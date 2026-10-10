#pragma once

#include "core/EchoCanceller.h"

#include <api/scoped_refptr.h>

#include <memory>

namespace webrtc {
class AudioProcessing;
}

namespace speecher {

// webrtc-audio-processing's AEC3, fed in 10 ms frames of each stream.
class WebRtcEchoCanceller final : public EchoCanceller {
public:
    // None when the audio processing cannot start, with its
    // AudioProcessing::Error in error.
    static std::unique_ptr<WebRtcEchoCanceller> create(int *error);
    ~WebRtcEchoCanceller() override;

    void addReference(const QByteArray &pcm) override;
    QByteArray process(const QByteArray &microphonePcm) override;
    QByteArray flush() override;

private:
    explicit WebRtcEchoCanceller(rtc::scoped_refptr<webrtc::AudioProcessing> processing);
    // Cancels whole microphone frames but the newest leadBytes, waiting for
    // system audio while at most maxWaitBytes more is held back.
    QByteArray cancelFrames(qsizetype leadBytes, qsizetype maxWaitBytes);
    void dropSpareReference();

    rtc::scoped_refptr<webrtc::AudioProcessing> m_processing;
    QByteArray m_reference;
    QByteArray m_microphone;
    qsizetype m_leastReferenceBacklog;
    int m_framesSinceSurplusCheck = 0;
};

} // namespace speecher
