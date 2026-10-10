#include "platform/audio/WebRtcEchoCanceller.h"

#include <modules/audio_processing/include/audio_processing.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace speecher {

// The microphone and system audio come from different devices whose clocks
// drift apart, by about a third of a second an hour at 100 ppm, and each
// arrives in chunks on its own schedule. AEC3 meets uneven render and capture
// calls with resets: a full one once more than 8 render blocks stay unread for
// 250 blocks, a delay reset at every underrun, and each costs seconds of
// cancellation. So every 10 ms microphone frame goes in with exactly one 10 ms
// frame of system audio, the next in its stream, and the alignment is kept
// here by dropping system audio or putting silence before it:
// - Arrival order is the only clock. Each time microphone audio arrives, the
//   microphone waiting less the system audio waiting is how much newer the
//   system audio next in line is than the microphone frame next in line, had
//   both streams just delivered. Pairing frames off leaves it alone. It reads
//   true just after system audio arrives, and up to a microphone chunk high
//   otherwise; system audio arriving late or in bursts only makes it larger,
//   so the smallest reading over a second is taken. Late microphone audio
//   makes it smaller, erring toward more lead.
// - AEC3 then gets system audio about that reading before the microphone
//   hears it, plus however much sooner than the speakers play it the system
//   audio arrives, less however much later than the microphone's it arrives.
//   AEC3 finds an echo from about 10 ms to 470 ms after its system audio, so
//   the reading is kept near kLeadBytes, 250 ms: system audio arriving up to
//   about 150 ms later than the microphone's still cancels.
// - AEC3 follows a slowly sliding echo, as drift slides it, up to about 400
//   ppm, but a sudden move costs it seconds, and a move toward less lead even
//   a frame does. So once the reading strays kSlideAfterBytes from the lead,
//   the pairing slides back a sample at a time at 312 ppm, which outpaces the
//   drift between real devices. Only a stray beyond kMoveAfterBytes, a start
//   offset or a stall, moves it at once.
// - The pairing starts at the lead, for streams that start together, and the
//   first reading realigns it.
// - A microphone frame held kMaxMicrophoneHeldBytes goes in with silence, and
//   the system audio it stood for is dropped when it comes, so a stall
//   shorter than a second does not move the pairing.
// - m_reference holds at most kMaxReferenceBytes, for a microphone that stops
//   while system audio keeps coming.
// - AEC3 returns the microphone kProcessingDelayBytes late: that much is
//   trimmed from the start, and flush feeds silence to push out the end.
namespace {

constexpr int kSampleRate = 16000;
constexpr qsizetype kSampleBytes = qsizetype(sizeof(int16_t));
constexpr qsizetype kFrameSamples = kSampleRate / 100;
constexpr qsizetype kFrameBytes = kFrameSamples * kSampleBytes;
constexpr qsizetype kLeadBytes = 25 * kFrameBytes;
constexpr qsizetype kSlideAfterBytes = 5 * kFrameBytes;
constexpr qsizetype kMoveAfterBytes = 15 * kFrameBytes;
// One sample every 20 frames, 312 ppm.
constexpr int kSlideFrames = 20;
constexpr qsizetype kMaxMicrophoneHeldBytes = kLeadBytes + 50 * kFrameBytes;
constexpr qsizetype kRealignBytes = 100 * kFrameBytes;
constexpr qsizetype kMaxReferenceBytes = 100 * kFrameBytes;
constexpr qsizetype kProcessingDelayBytes = 128 * kSampleBytes;

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
    , m_owedReference(kLeadBytes)
    , m_leastPairedLead(std::numeric_limits<qsizetype>::max())
    , m_microphoneSinceRealign(kRealignBytes)
    , m_outputToSkip(kProcessingDelayBytes)
{
}

WebRtcEchoCanceller::~WebRtcEchoCanceller() = default;

void WebRtcEchoCanceller::addReference(const QByteArray &pcm)
{
    m_referenceArrived = true;
    const qsizetype owed = std::min(m_owedReference, pcm.size());
    m_owedReference -= owed;
    m_reference.append(pcm.sliced(owed));
    if (m_reference.size() > kMaxReferenceBytes) {
        m_reference.remove(0, m_reference.size() - kMaxReferenceBytes);
    }
}

QByteArray WebRtcEchoCanceller::process(const QByteArray &microphonePcm)
{
    m_microphone.append(microphonePcm);
    if (m_referenceArrived) {
        m_leastPairedLead = std::min(m_leastPairedLead, pairedLead());
        m_microphoneSinceRealign += microphonePcm.size();
        if (m_microphoneSinceRealign >= kRealignBytes) {
            realign();
        }
    }
    return cancelFrames(kMaxMicrophoneHeldBytes);
}

QByteArray WebRtcEchoCanceller::flush()
{
    const qsizetype unframed = m_microphone.size() + kProcessingDelayBytes;
    const qsizetype padding = (unframed + kFrameBytes - 1) / kFrameBytes * kFrameBytes - m_microphone.size();
    m_microphone.append(QByteArray(padding, '\0'));
    return cancelFrames(0).chopped(padding - kProcessingDelayBytes);
}

qsizetype WebRtcEchoCanceller::pairedLead() const
{
    return m_microphone.size() - (m_reference.size() - m_owedReference);
}

void WebRtcEchoCanceller::realign()
{
    const qsizetype stray = m_leastPairedLead - kLeadBytes;
    m_slideBytes = 0;
    if (stray < -kMoveAfterBytes) {
        dropReference(-stray / kFrameBytes * kFrameBytes);
    } else if (stray > kMoveAfterBytes) {
        padReference(stray / kFrameBytes * kFrameBytes);
    } else if (std::abs(stray) > kSlideAfterBytes) {
        m_slideBytes = stray < 0 ? -kSampleBytes : kSampleBytes;
    }
    m_leastPairedLead = std::numeric_limits<qsizetype>::max();
    m_microphoneSinceRealign = 0;
}

void WebRtcEchoCanceller::slide()
{
    if (m_slideBytes == 0 || ++m_framesSinceSlide < kSlideFrames) {
        return;
    }
    m_framesSinceSlide = 0;
    if (m_slideBytes < 0) {
        dropReference(-m_slideBytes);
    } else {
        padReference(m_slideBytes);
    }
}

void WebRtcEchoCanceller::dropReference(qsizetype bytes)
{
    const qsizetype dropped = std::min(bytes, m_reference.size());
    m_reference.remove(0, dropped);
    m_owedReference += bytes - dropped;
}

void WebRtcEchoCanceller::padReference(qsizetype bytes)
{
    const qsizetype forgiven = std::min(bytes, m_owedReference);
    m_owedReference -= forgiven;
    m_reference.prepend(QByteArray(bytes - forgiven, '\0'));
}

QByteArray WebRtcEchoCanceller::cancelFrames(qsizetype maxHeldBytes)
{
    const webrtc::StreamConfig stream(kSampleRate, 1);
    std::array<int16_t, kFrameSamples> render;
    std::array<int16_t, kFrameSamples> capture;
    QByteArray cancelled;
    while (m_microphone.size() >= kFrameBytes) {
        if (m_reference.size() >= kFrameBytes) {
            std::memcpy(render.data(), m_reference.constData(), kFrameBytes);
            m_reference.remove(0, kFrameBytes);
        } else if (m_microphone.size() > maxHeldBytes) {
            render.fill(0);
            dropReference(kFrameBytes);
        } else {
            break;
        }
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
        slide();
    }
    const qsizetype skipped = std::min(m_outputToSkip, cancelled.size());
    m_outputToSkip -= skipped;
    return cancelled.sliced(skipped);
}

} // namespace speecher
