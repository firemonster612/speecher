#pragma once

// A Recording's audio, kept while Keep the call's audio is on: both sides in
// one stereo file beside the transcript, me on the left and them on the
// right, each placed on the recording's clock so one file plays both in step.

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>

namespace speecher {

class HistoryAudioWriter;

// Two mono streams side by side on one clock, as interleaved stereo frames.
// A chunk goes where it ended; a gap longer than kGapMs before it (a stream
// that sent nothing for a while) is silence, and shorter ones are the jitter
// of chunks arriving, which closes up. A channel more than kLagMs behind the
// other is filled with silence, so neither waits long on a stream that has
// stopped.
class StereoTimeline {
public:
    static constexpr int kGapMs = 200;
    static constexpr int kLagMs = 2000;

    // pcm: 16 kHz mono 16-bit samples of channel 0 (left) or 1 (right) that
    // ended endMs into the recording.
    void place(int channel, qint64 endMs, const QByteArray &pcm);
    // The frames both channels have filled, which leave the timeline.
    QByteArray take();
    // Every frame left, the shorter channel filled with silence.
    QByteArray takeAll();

private:
    qint64 endOf(int channel) const;
    void fillTo(int channel, qint64 sample);
    QByteArray interleave(qint64 frames);

    // Samples not taken yet, per channel, starting at m_taken.
    QByteArray m_pending[2];
    qint64 m_taken = 0;
};

// The stereo file of one recording, <transcript's name>.m4a (or .wav) beside
// it, written as the two sides are heard.
class RecordingAudio : public QObject {
    Q_OBJECT

public:
    explicit RecordingAudio(const QString &transcriptPath, QObject *parent = nullptr);
    // Discards what was not finished; one finishing finishes on its own.
    ~RecordingAudio() override;

    // Channel 0 is the microphone, 1 system audio; see StereoTimeline::place.
    void hear(int channel, qint64 endMs, const QByteArray &pcm);
    // Ends the file; finished follows, perhaps before this returns.
    void finish();
    // Throws the audio away.
    void discard();

signals:
    // Once, after finish(): the file's path once it is written, or empty
    // with why it couldn't be (empty too when nothing was heard). A writer
    // that failed while the recording ran says so here.
    void finished(const QString &path, const QString &error);

private:
    QString m_folder;
    StereoTimeline m_timeline;
    // Gone once it has finished, or failed on the way.
    QPointer<HistoryAudioWriter> m_writer;
    bool m_finishing = false;
    // What the writer said when it finished.
    bool m_written = false;
    QString m_path;
    QString m_error;
};

} // namespace speecher
