#include "platform/audio/WavFileAudioInput.h"

#include <QFile>
#include <QtEndian>

#include <algorithm>
#include <cmath>

namespace speecher {
namespace {

// 100 ms of 16 kHz mono s16.
constexpr int kChunkMs = 100;
constexpr qsizetype kChunkBytes = 16000 * 2 * kChunkMs / 1000;

// Returns the payload of the first "data" chunk, empty when the file is not a
// parseable WAV. The seam expects 16 kHz mono s16 and does not convert.
QByteArray wavDataChunk(const QByteArray &file)
{
    if (!file.startsWith(QByteArrayLiteral("RIFF")) || file.mid(8, 4) != QByteArrayLiteral("WAVE")) {
        return {};
    }
    qsizetype cursor = 12;
    while (cursor + 8 <= file.size()) {
        const QByteArray id = file.mid(cursor, 4);
        // Wide arithmetic and a bounds check: a hostile or truncated chunk
        // size would otherwise wrap the cursor and loop forever.
        const qsizetype size = qsizetype(qFromLittleEndian<quint32>(file.constData() + cursor + 4));
        if (size > file.size() - cursor - 8) {
            return {};
        }
        if (id == QByteArrayLiteral("data")) {
            return file.mid(cursor + 8, size);
        }
        cursor += 8 + size + (size % 2);
    }
    return {};
}

} // namespace

WavFileAudioInput::WavFileAudioInput(const QString &path, QObject *parent)
    : AudioInput(parent)
    , m_path(path)
{
    m_timer.setInterval(kChunkMs);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &WavFileAudioInput::emitChunk);
}

bool WavFileAudioInput::start(QString *error)
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("Could not open SPEECHER_AUDIO_WAV file %1").arg(m_path);
        }
        return false;
    }
    m_pcm = wavDataChunk(file.readAll());
    if (m_pcm.isEmpty()) {
        if (error) {
            *error = QStringLiteral("SPEECHER_AUDIO_WAV file %1 is not a usable WAV").arg(m_path);
        }
        return false;
    }
    m_offset = 0;
    m_timer.start();
    return true;
}

void WavFileAudioInput::stop()
{
    m_timer.stop();
    m_pcm.clear();
    m_offset = 0;
}

bool WavFileAudioInput::isActive() const
{
    return m_timer.isActive();
}

void WavFileAudioInput::emitChunk()
{
    QByteArray chunk;
    if (m_offset < m_pcm.size()) {
        chunk = m_pcm.mid(m_offset, kChunkBytes);
        m_offset += chunk.size();
    }
    if (chunk.size() < kChunkBytes) {
        chunk.append(kChunkBytes - chunk.size(), '\0');
    }
    float peak = 0.0f;
    const auto *samples = reinterpret_cast<const qint16 *>(chunk.constData());
    for (qsizetype i = 0; i < chunk.size() / 2; ++i) {
        peak = std::max(peak, std::abs(samples[i]) / 32768.0f);
    }
    emit audioChunk(chunk);
    emit levelChanged(peak);
}

} // namespace speecher
