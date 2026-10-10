#include "platform/audio/LoopbackPcm.h"

namespace speecher {
namespace {

constexpr qint64 kSamplesPerMs = 16;
// Packets land a little off the end of the stream, from rounding and the
// output's clock drifting from the system's, so a shorter gap is left to a
// later fill rather than padded into the middle of audio.
constexpr qint64 kShortestGapMs = 20;
// A longer gap is the machine asleep or capture stalled, not silence that
// played, and the stream picks up after it instead of sending it.
constexpr qint64 kLongestGapMs = 5000;

} // namespace

void LoopbackPcm::useFormat(const QAudioFormat &format)
{
    m_format = format;
    m_converter.reset(format);
}

QByteArray LoopbackPcm::convert(const QByteArray &packet)
{
    const QByteArray pcm = m_converter.convert(packet).pcm16Mono16k;
    m_samples += pcm.size() / qsizetype(sizeof(qint16));
    return pcm;
}

QByteArray LoopbackPcm::convertSilent(qsizetype bytes)
{
    // Unsigned samples are silent at the middle of their range.
    const char silence = m_format.sampleFormat() == QAudioFormat::UInt8 ? char(0x80) : '\0';
    return convert(QByteArray(bytes, silence));
}

QByteArray LoopbackPcm::silenceUntil(qint64 elapsedMs)
{
    const qint64 owed = elapsedMs * kSamplesPerMs - m_samples;
    if (owed < kShortestGapMs * kSamplesPerMs) {
        return {};
    }
    m_samples += owed;
    if (owed > kLongestGapMs * kSamplesPerMs) {
        return {};
    }
    return QByteArray(owed * qsizetype(sizeof(qint16)), '\0');
}

} // namespace speecher
