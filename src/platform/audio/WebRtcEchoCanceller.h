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
    // How much newer the system audio next in line is than the microphone
    // frame next in line, had both streams just delivered.
    qsizetype pairedLead() const;
    void realign();
    // Drops one system audio sample, or puts one of silence before it, every
    // kSlideFrames frames while the pairing slides.
    void slide();
    void dropReference(qsizetype bytes);
    void padReference(qsizetype bytes);
    // Cancels whole microphone frames, waiting for system audio while at most
    // maxHeldBytes of the microphone is held back.
    QByteArray cancelFrames(qsizetype maxHeldBytes);

    rtc::scoped_refptr<webrtc::AudioProcessing> m_processing;
    QByteArray m_reference;
    QByteArray m_microphone;
    // System audio already stood in for by silence or skipped ahead of
    // arriving, dropped as it arrives.
    qsizetype m_owedReference;
    bool m_referenceArrived = false;
    // The smallest pairedLead since the pairing last realigned, and the
    // microphone audio since, which starts full so the first reading
    // realigns.
    qsizetype m_leastPairedLead;
    qsizetype m_microphoneSinceRealign;
    // Silence put before system audio each slide step; negative, system audio
    // dropped.
    qsizetype m_slideBytes = 0;
    int m_framesSinceSlide = 0;
    // What AEC3 returns before the first microphone audio.
    qsizetype m_outputToSkip;
};

} // namespace speecher
