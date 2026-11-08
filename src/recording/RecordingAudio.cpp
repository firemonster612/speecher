#include "recording/RecordingAudio.h"

#include "core/history/HistoryAudioWriter.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <utility>

namespace speecher {
namespace {

constexpr qint64 samplesPerMs = 16;
constexpr int bytesPerSample = 2;

} // namespace

qint64 StereoTimeline::endOf(int channel) const
{
    return m_taken + m_pending[channel].size() / bytesPerSample;
}

void StereoTimeline::fillTo(int channel, qint64 sample)
{
    const qint64 missing = sample - endOf(channel);
    if (missing > 0) {
        m_pending[channel].append(QByteArray(missing * bytesPerSample, '\0'));
    }
}

void StereoTimeline::place(int channel, qint64 endMs, const QByteArray &pcm)
{
    const qint64 start = endMs * samplesPerMs - pcm.size() / bytesPerSample;
    if (start - endOf(channel) > kGapMs * samplesPerMs) {
        fillTo(channel, start);
    }
    m_pending[channel].append(pcm);
    fillTo(1 - channel, endOf(channel) - kLagMs * samplesPerMs);
}

QByteArray StereoTimeline::take()
{
    return interleave(std::min(endOf(0), endOf(1)) - m_taken);
}

QByteArray StereoTimeline::takeAll()
{
    const qint64 end = std::max(endOf(0), endOf(1));
    fillTo(0, end);
    fillTo(1, end);
    return interleave(end - m_taken);
}

QByteArray StereoTimeline::interleave(qint64 frames)
{
    QByteArray stereo(frames * 2 * bytesPerSample, Qt::Uninitialized);
    const char *left = m_pending[0].constData();
    const char *right = m_pending[1].constData();
    char *out = stereo.data();
    for (qint64 frame = 0; frame < frames; ++frame) {
        std::copy_n(left + frame * bytesPerSample, bytesPerSample, out);
        std::copy_n(right + frame * bytesPerSample, bytesPerSample, out + bytesPerSample);
        out += 2 * bytesPerSample;
    }
    m_pending[0].remove(0, frames * bytesPerSample);
    m_pending[1].remove(0, frames * bytesPerSample);
    m_taken += frames;
    return stereo;
}

// The writer finishes on its own once asked to and deletes itself, so it has
// no parent that could cut its encoding short.
RecordingAudio::RecordingAudio(const QString &transcriptPath, QObject *parent)
    : QObject(parent)
    , m_folder(QFileInfo(transcriptPath).absolutePath())
    , m_writer(new HistoryAudioWriter(m_folder, QFileInfo(transcriptPath).completeBaseName(), 2))
{
}

RecordingAudio::~RecordingAudio()
{
    discard();
}

void RecordingAudio::hear(int channel, qint64 endMs, const QByteArray &pcm)
{
    if (!m_writer) {
        return;
    }
    m_timeline.place(channel, endMs, pcm);
    const QByteArray ready = m_timeline.take();
    if (!ready.isEmpty()) {
        m_writer->write(ready);
    }
}

QString RecordingAudio::finish()
{
    if (!m_writer) {
        return {};
    }
    m_writer->write(m_timeline.takeAll());
    const QString name = std::exchange(m_writer, nullptr)->finish();
    return name.isEmpty() ? QString() : QDir(m_folder).filePath(name);
}

void RecordingAudio::discard()
{
    if (m_writer) {
        std::exchange(m_writer, nullptr)->discard();
    }
}

} // namespace speecher
