#include "app/MicrophoneTest.h"

#include "app/ApplicationController.h"
#include "app/PlatformComposition.h"
#include "core/SettingsStore.h"
#include "dictation/DictationPorts.h"
#include "dictation/DictationTypes.h"

#include <QPointer>

#include <utility>

namespace speecher {

namespace {

// A level above this is input, not a dead or muted microphone.
constexpr float kHeardLevel = 0.01f;

} // namespace

MicrophoneTest::MicrophoneTest(ApplicationController &controller,
                               QObject *parent,
                               InputFactory createInput)
    : QObject(parent)
    , m_createInput(std::move(createInput))
    , m_dictationListening(dictationListeningPresentation(controller.stateName()))
{
    if (!m_createInput) {
        m_createInput = [&controller](QObject *owner) {
            return controller.platform()->createAudioInput(controller.settings(), owner);
        };
    }
    connect(&controller, &ApplicationController::stateChanged, this, [this](const QString &state) {
        m_dictationListening = dictationListeningPresentation(state);
        if (m_dictationListening) {
            stop();
        }
        emit changed();
    });
    // The UI saves the device as it is chosen, and a running test moves to
    // it so it hears the microphone that will record. What it heard was the
    // old device, so that goes even while Stopped.
    connect(controller.settings(), &SettingsStore::audioCaptureSettingsChanged, this,
            [this](const AudioCaptureSettings &settings) {
        if (settings.deviceId == m_deviceId) {
            return;
        }
        m_deviceId = settings.deviceId;
        if (std::exchange(m_heard, false)) {
            emit changed();
        }
        if (m_state == MicrophoneTestState::Starting) {
            stop();
            return;
        }
        // Queued: start() can wait in a nested event loop, which must not run
        // inside the settings store's signal (a CLI's config set over IPC).
        QMetaObject::invokeMethod(this, [this, deviceId = settings.deviceId] {
            if (deviceId != m_deviceId || m_state != MicrophoneTestState::Running) {
                return;
            }
            stop();
            start(deviceId);
        }, Qt::QueuedConnection);
    });
}

MicrophoneTest::~MicrophoneTest()
{
    // Releases the input without a word: whatever listens to changed may be
    // halfway destroyed itself.
    if (!m_input) {
        return;
    }
    disconnect(m_input, nullptr, this, nullptr);
    m_input->setParent(nullptr);
    // While Starting, the input's start() is still on the stack below, and
    // start() frees the input once it returns and finds this test gone.
    if (m_state != MicrophoneTestState::Starting) {
        m_input->deleteLater();
    }
}

bool MicrophoneTest::canToggle() const
{
    return m_state != MicrophoneTestState::Starting && !m_dictationListening;
}

void MicrophoneTest::start(const QString &deviceId)
{
    if (m_state != MicrophoneTestState::Stopped || !canToggle()) {
        return;
    }
    m_deviceId = deviceId;
    if (std::exchange(m_heard, false)) {
        emit changed();
    }
    AudioInput *input = m_createInput(this);
    input->useDevice(deviceId);
    connect(input, &AudioInput::levelChanged, this, [this](float level) {
        if (level > kHeardLevel && !std::exchange(m_heard, true)) {
            emit changed();
        }
        emit levelChanged(level);
    });
    connect(input, &AudioInput::failed, this, [this, deviceId](const QString &message) {
        // While Starting, start() reports it once the input's start() returns.
        if (m_state == MicrophoneTestState::Starting) {
            m_startFailure = message;
            m_stopRequested = true;
            return;
        }
        // What it heard came from a device that has gone.
        m_heard = false;
        stop();
        emit failed(deviceId, message);
    });
    m_input = input;
    setState(MicrophoneTestState::Starting);

    // The input's start() can wait for the device in a nested event loop, in
    // which anything may stop or destroy this test.
    const QPointer<MicrophoneTest> alive(this);
    QString error;
    const bool started = input->start(&error);
    if (!alive) {
        input->deleteLater();
        return;
    }
    const bool stopRequested = std::exchange(m_stopRequested, false);
    // The input's own reason beats the timeout its start() then gives.
    const QString failure = std::exchange(m_startFailure, QString());
    if (!started || stopRequested) {
        closeDevice();
        if (!failure.isEmpty() || !started) {
            emit failed(deviceId, failure.isEmpty() ? error : failure);
        }
        return;
    }
    setState(MicrophoneTestState::Running);
}

void MicrophoneTest::stop()
{
    if (m_state == MicrophoneTestState::Starting) {
        m_stopRequested = true;
        return;
    }
    closeDevice();
}

void MicrophoneTest::setState(MicrophoneTestState state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit changed();
}

void MicrophoneTest::closeDevice()
{
    if (!m_input) {
        return;
    }
    AudioInput *input = std::exchange(m_input, nullptr);
    disconnect(input, nullptr, this, nullptr);
    // Destroyed rather than stopped: a stopped input set to keep the
    // microphone open between dictations reopens the device to stay warm.
    // Later, because a stop can arrive from inside the input's own signal.
    input->deleteLater();
    setState(MicrophoneTestState::Stopped);
}

} // namespace speecher
