#include "platform/audio/LoopbackPcm.h"

namespace speecher {
namespace {

constexpr qint64 kSamplesPerMs = 16;

} // namespace

void LoopbackPcm::useFormat(const QAudioFormat &format)
{
    m_converter.reset(format);
}

QByteArray LoopbackPcm::convert(const QByteArray &packet)
{
    const QByteArray pcm = m_converter.convert(packet).pcm16Mono16k;
    m_samples += pcm.size() / qsizetype(sizeof(qint16));
    return pcm;
}

QByteArray LoopbackPcm::silenceUntil(qint64 elapsedMs)
{
    const qint64 owed = elapsedMs * kSamplesPerMs - m_samples;
    if (owed <= 0) {
        return {};
    }
    m_samples += owed;
    return QByteArray(owed * qsizetype(sizeof(qint16)), '\0');
}

} // namespace speecher
