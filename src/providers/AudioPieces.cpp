#include "providers/AudioPieces.h"

#include <QtEndian>

#include <algorithm>
#include <cmath>

namespace speecher {
namespace {

// 100 ms is quiet at this share of the loudest 100 ms of its piece, as RMS:
// 20 dB under it.
constexpr float kQuietShareOfLoudest = 0.1f;

float rmsOf(QByteArrayView pcm)
{
    const qsizetype samples = pcm.size() / 2;
    if (samples == 0) {
        return 0.0f;
    }
    double sum = 0.0;
    for (qsizetype index = 0; index < samples; ++index) {
        const double sample = qFromLittleEndian<qint16>(pcm.constData() + 2 * index) / 32768.0;
        sum += sample * sample;
    }
    return float(std::sqrt(sum / double(samples)));
}

} // namespace

AudioPieces::Fit AudioPieces::fit(QByteArrayView pcm)
{
    for (qsizetype at = 0; at < pcm.size();) {
        const QByteArrayView window =
            pcm.sliced(at, std::min({kSpeechChunkBytes, pcm.size() - at, kLongestPieceBytes - m_bytes}));
        at += window.size();
        m_bytes += window.size();
        const float rms = rmsOf(window);
        m_loudest = std::max(m_loudest, rms);
        m_quietBytes = rms <= m_loudest * kQuietShareOfLoudest ? m_quietBytes + window.size() : 0;
        if (m_bytes >= kLongestPieceBytes || (m_bytes >= kShortestPieceBytes && m_quietBytes >= kPauseBytes)) {
            *this = {};
            return {at, true};
        }
    }
    return {pcm.size(), false};
}

} // namespace speecher
