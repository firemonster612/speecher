#pragma once

#include "core/AppSettings.h"
#include "platform/win/WinCaptureInput.h"

#include <QList>

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

namespace speecher {

// The endpoint id of the microphone the audio settings name by deviceId,
// which is Qt's id in base64url; Qt's id on Windows is the endpoint's.
QString microphoneEndpointId(const QString &deviceId);

// Why a communications capture records without Windows' echo cancellation,
// or nothing when it has it, from how setting the stream's communications
// category went, how asking for the stream's effects went, and the effects
// it has. A Windows without the effects manager fails that ask.
QString communicationsEchoCancellationWarning(HRESULT category,
                                              HRESULT effectsQuery,
                                              const QList<AUDIO_EFFECT> &effects);

// Keeps an echo canceller's reference on the default output, the output
// system audio captures, from its construction on. Lives on one thread,
// which calls update between reads.
class EchoReferenceFollower {
public:
    EchoReferenceFollower(IMMDeviceEnumerator *enumerator, IAcousticEchoCancellationControl *control);
    ~EchoReferenceFollower();
    Q_DISABLE_COPY_MOVE(EchoReferenceFollower)

    // Sets the reference again when the default output changed since.
    void update();

private:
    void setReference();

    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> m_enumerator;
    Microsoft::WRL::ComPtr<IAcousticEchoCancellationControl> m_control;
    Microsoft::WRL::ComPtr<DefaultOutputWatcher> m_watcher;
};

// The microphone for a Recording, captured with WASAPI as a communications
// stream, which brings in the echo cancellation of a microphone whose driver
// has it, with the default output, followed as it changes, as what it
// cancels where Windows lets Speecher say so. Delivered as 16 kHz mono s16.
// Of the audio settings it takes the microphone, where an empty or missing
// one means the default, and the post-roll; the rest shape dictation's
// listening. Qt Multimedia cannot set a stream's category, which is why
// dictation's microphone, which needs no echo cancellation, is Qt's and this
// one is not.
class WinCommunicationsAudioInput final : public WinCaptureInput {
public:
    explicit WinCommunicationsAudioInput(const AudioCaptureSettings &settings, QObject *parent = nullptr);
};

} // namespace speecher
