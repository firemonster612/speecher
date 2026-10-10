#include "platform/audio/WebRtcEchoCanceller.h"

#include <modules/audio_processing/include/audio_processing.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace speecher {

// The microphone and system audio come from different devices whose clocks
// drift apart, by about a third of a second an hour at 100 ppm. AEC3 meets
// uneven render and capture calls with resets: a full one once more than 8
// render blocks stay unread for 250 blocks, a delay reset at every underrun,
// and each costs seconds of cancellation. So every 10 ms microphone frame goes
// in with exactly one 10 ms frame of system audio, the pairing starting with
// the first of each, and the drift comes out here a frame at a time, which
// AEC3's delay estimator follows without a reset:
// - The newest kMicrophoneLeadBytes of the microphone always wait, so the
//   system audio a frame goes in with is about that much newer than it, and
//   AEC3 has seen what the speakers played before it hears the echo.
// - System audio waits in m_reference. Once at least a frame of it has been
//   waiting for a whole second, it has run ahead, and those spare frames are
//   dropped, oldest first. The two streams arrive in chunks, so this sees the
//   drift only a microphone chunk (50 ms) at a time; the lead covers that.
// - When system audio runs behind, microphone frames wait for it, up to
//   kMaxMicrophoneWaitBytes beyond the lead, as system audio arrives 100 ms
//   at a time. After that a frame goes in with silence instead.
// - m_reference holds at most kMaxReferenceBytes, for a microphone that stops
//   while system audio keeps coming.
// Within that pairing, AEC3's own delay estimator finds where the echo is.
namespace {

constexpr int kSampleRate = 16000;
constexpr qsizetype kFrameSamples = kSampleRate / 100;
constexpr qsizetype kFrameBytes = kFrameSamples * qsizetype(sizeof(int16_t));
constexpr qsizetype kMicrophoneLeadBytes = 10 * kFrameBytes;
constexpr qsizetype kMaxMicrophoneWaitBytes = 10 * kFrameBytes;
constexpr int kSurplusCheckFrames = 100;
constexpr qsizetype kMaxReferenceBytes = 100 * kFrameBytes;

} // namespace

std::unique_ptr<WebRtcEchoCanceller> WebRtcEchoCanceller::create(int *error)
{
    rtc::scoped_refptr<webrtc::AudioProcessing> processing(webrtc::AudioProcessingBuilder().Create());
    if (!processing) {
        *error = webrtc::AudioProcessing::kCreationFailedError;
        return nullptr;
    }
    webrtc::AudioProcessing::Config config;
    config.echo_canceller.enabled = true;
    // AEC3; mobile mode is the cheaper AECM.
    config.echo_canceller.mobile_mode = false;
    processing->ApplyConfig(config);
    const webrtc::StreamConfig stream(kSampleRate, 1);
    const int result = processing->Initialize(webrtc::ProcessingConfig{{stream, stream, stream, stream}});
    if (result != webrtc::AudioProcessing::kNoError) {
        *error = result;
        return nullptr;
    }
    return std::unique_ptr<WebRtcEchoCanceller>(new WebRtcEchoCanceller(std::move(processing)));
}

WebRtcEchoCanceller::WebRtcEchoCanceller(rtc::scoped_refptr<webrtc::AudioProcessing> processing)
    : m_processing(std::move(processing))
    , m_leastReferenceBacklog(kMaxReferenceBytes)
{
}

WebRtcEchoCanceller::~WebRtcEchoCanceller() = default;

void WebRtcEchoCanceller::addReference(const QByteArray &pcm)
{
    m_reference.append(pcm);
    if (m_reference.size() > kMaxReferenceBytes) {
        m_reference.remove(0, m_reference.size() - kMaxReferenceBytes);
    }
}

QByteArray WebRtcEchoCanceller::process(const QByteArray &microphonePcm)
{
    m_microphone.append(microphonePcm);
    return cancelFrames(kMicrophoneLeadBytes, kMaxMicrophoneWaitBytes);
}

QByteArray WebRtcEchoCanceller::flush()
{
    const qsizetype padding = (kFrameBytes - m_microphone.size() % kFrameBytes) % kFrameBytes;
    m_microphone.append(QByteArray(padding, '\0'));
    return cancelFrames(0, 0).chopped(padding);
}

QByteArray WebRtcEchoCanceller::cancelFrames(qsizetype leadBytes, qsizetype maxWaitBytes)
{
    const webrtc::StreamConfig stream(kSampleRate, 1);
    std::array<int16_t, kFrameSamples> render;
    std::array<int16_t, kFrameSamples> capture;
    QByteArray cancelled;
    while (m_microphone.size() - leadBytes >= kFrameBytes) {
        if (m_reference.size() >= kFrameBytes) {
            std::memcpy(render.data(), m_reference.constData(), kFrameBytes);
            m_reference.remove(0, kFrameBytes);
        } else if (m_microphone.size() - leadBytes > maxWaitBytes) {
            render.fill(0);
        } else {
            break;
        }
        dropSpareReference();
        std::memcpy(capture.data(), m_microphone.constData(), kFrameBytes);
        const bool processed =
            m_processing->ProcessReverseStream(render.data(), stream, stream, render.data())
                == webrtc::AudioProcessing::kNoError
            && m_processing->ProcessStream(capture.data(), stream, stream, capture.data())
                   == webrtc::AudioProcessing::kNoError;
        // A frame AEC3 refuses goes out as it came in.
        cancelled.append(processed ? QByteArray(reinterpret_cast<const char *>(capture.data()), kFrameBytes)
                                   : m_microphone.first(kFrameBytes));
        m_microphone.remove(0, kFrameBytes);
    }
    return cancelled;
}

void WebRtcEchoCanceller::dropSpareReference()
{
    m_leastReferenceBacklog = std::min(m_leastReferenceBacklog, m_reference.size());
    if (++m_framesSinceSurplusCheck < kSurplusCheckFrames) {
        return;
    }
    m_reference.remove(0, m_leastReferenceBacklog / kFrameBytes * kFrameBytes);
    m_leastReferenceBacklog = m_reference.size();
    m_framesSinceSurplusCheck = 0;
}

} // namespace speecher
