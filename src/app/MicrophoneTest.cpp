#include "app/MicrophoneTest.h"

#include "app/ApplicationController.h"
#include "app/PlatformComposition.h"
#include "core/SettingsStore.h"
#include "dictation/DictationPorts.h"
#include "dictation/DictationTypes.h"

#include <utility>

namespace speecher {

MicrophoneTest::MicrophoneTest(ApplicationController &controller, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
{
    connect(&controller, &ApplicationController::stateChanged, this, [this](const QString &state) {
        if (dictationListeningPresentation(state)) {
            stop();
        }
    });
    connect(controller.settings(), &SettingsStore::audioCaptureSettingsChanged, this,
            [this](const AudioCaptureSettings &settings) {
        if (settings.deviceId != m_deviceId) {
            stop();
        }
    });
}

MicrophoneTest::~MicrophoneTest()
{
    stop();
}

void MicrophoneTest::start()
{
    if (m_input) {
        return;
    }
    SettingsStore *settings = m_controller.settings();
    m_deviceId = settings->audioInputDeviceId();
    m_input = m_controller.platform()->createAudioInput(settings, this);
    connect(m_input, &AudioInput::levelChanged, this, &MicrophoneTest::levelChanged);
    connect(m_input, &AudioInput::failed, this, [this](const QString &message) {
        stop();
        emit failed(message);
    });
    QString error;
    if (!m_input->start(&error)) {
        stop();
        emit failed(error);
        return;
    }
    emit runningChanged(true);
}

void MicrophoneTest::stop()
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
    emit runningChanged(false);
}

} // namespace speecher
