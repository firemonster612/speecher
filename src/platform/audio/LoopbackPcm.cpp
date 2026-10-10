#include "platform/audio/LoopbackPcm.h"

namespace speecher {
namespace {

constexpr qint64 kSamplesPerMs = 16;
// Packets land a little off the end of the stream, from rounding and the
// output's clock drifting from the system's, so a shorter gap is left to a
// later fill rather than padded into the middle of audio.
constexpr qint64 kShortestGapMs = 20;

} // namespace

qint64 loopbackPacketMs(qint64 elapsedMs, qint64 counterNowMs, qint64 counterPlayedAtMs)
{
    return elapsedMs - (counterNowMs - counterPlayedAtMs);
}

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
    return convert(silentPcm(m_format, bytes));
}

QList<QByteArray> LoopbackPcm::silenceUntil(qint64 elapsedMs)
{
    qint64 owed = elapsedMs * kSamplesPerMs - m_samples;
    if (owed < kShortestGapMs * kSamplesPerMs) {
        return {};
    }
    m_samples += owed;
    // A gap as long as a stall comes in pieces, so no chunk holds more than a
    // poll's worth.
    QList<QByteArray> chunks;
    for (; owed > 0; owed -= kLoopbackPollMs * kSamplesPerMs) {
        chunks.append(QByteArray(qMin(owed, kLoopbackPollMs * kSamplesPerMs) * qsizetype(sizeof(qint16)), '\0'));
    }
    return chunks;
}

} // namespace speecher
