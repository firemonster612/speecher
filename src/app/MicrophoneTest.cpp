#include "app/MicrophoneTest.h"

#include "app/ApplicationController.h"
#include "app/PlatformComposition.h"
#include "core/SettingsStore.h"
#include "dictation/DictationPorts.h"
#include "dictation/DictationTypes.h"


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
    , m_dictationListening(dictationListeningPresentation(controller.dictationState()))
{
    if (!m_createInput) {
        m_createInput = [&controller](QObject *owner) {
            return controller.platform()->createAudioInput(controller.settings(), owner);
        };
    }
    connect(&controller, &ApplicationController::stateChanged, this, [this](DictationState state) {
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
        if (m_state == MicrophoneTestState::Running) {
            stop();
            start(settings.deviceId);
        }
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
    m_input->deleteLater();
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
    connect(input, &AudioInput::started, this, [this] { setState(MicrophoneTestState::Running); });
    connect(input, &AudioInput::failed, this, [this, deviceId](const QString &message) {
        // What it heard came from a device that has gone.
        if (m_state == MicrophoneTestState::Running) {
            m_heard = false;
        }
        closeDevice();
        emit failed(deviceId, message);
    });
    m_input = input;
    setState(MicrophoneTestState::Starting);
    input->start();
}

void MicrophoneTest::stop()
{
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
