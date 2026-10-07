#pragma once

#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <functional>

namespace speecher {

class AudioInput;
class FileTranscriptionSession;
class ProviderRegistry;
class SettingsStore;
struct TranscribeFileResult;

// Where a recording goes when the command names no file:
// <dataFolder>/recordings/<yyyy-mm-dd-hhmm>.md.
QString defaultRecordingPath(const QString &dataFolder, const QDateTime &startedAt);

// A recording's transcript on disk: one "[hh:mm:ss] speaker: text" line per
// utterance, appended and flushed as it comes, so `tail -f` follows it.
class RecordingTranscript {
public:
    // Creates the file at path, or beside it as <name>-2.<suffix> and so on
    // rather than open one that exists. False with error set when it cannot.
    bool create(const QString &path, QString *error);
    QString path() const;
    bool append(qint64 elapsedMs, const QString &speaker, const QString &text, QString *error);
    void close();

private:
    QFile m_file;
};

// One audio source of a recording and the speaker its lines name.
struct RecordingStream {
    QString speaker;
    bool running = false;
    // Why the stream stopped, or why its last line could not be written.
    QString problem;
};

struct RecordingStatus {
    bool recording = false;
    QString path;
    qint64 durationMs = 0;
    QList<RecordingStream> streams;
};

// How `record status --json` and the IPC response carry a status.
QJsonObject recordingStatusJson(const RecordingStatus &status);
RecordingStatus recordingStatusFromJson(const QJsonObject &object);

// Records the microphone into a transcript file until stop(), with its own
// microphone input and providers, beside any dictation. Each final text the
// speech provider streams becomes a line, timed from the recording's start;
// partials are never written. A line carries the time its text was
// finalised, as streamed finals come with no timings. The recording ends an
// utterance itself at a pause or once it runs long, so a provider that
// finalizes only when asked (Codex) still writes lines while it runs.
class RecordingSession : public QObject {
    Q_OBJECT

public:
    using MicrophoneFactory = std::function<AudioInput *(QObject *parent)>;

    RecordingSession(SettingsStore *settings,
                     ProviderRegistry *providers,
                     MicrophoneFactory createMicrophone,
                     QObject *parent = nullptr);
    ~RecordingSession() override;

    // From start() until stopped is emitted.
    bool isRecording() const;
    RecordingStatus status() const;
    // Records into path, or the default file in dataFolder when path is
    // empty. Returns the file it writes, or empty with error set: already
    // recording, a speech provider that does not stream final text, or a file
    // that cannot be created.
    QString start(const QString &path, const QString &dataFolder, QString *error);
    // Stops the microphone; the last utterance is still written before
    // stopped is emitted.
    void stop();

signals:
    void recordingChanged(bool recording);
    // The finished recording's last status.
    void stopped(const speecher::RecordingStatus &status);

private:
    void trackUtterance(const QByteArray &pcm);
    void endUtterance();
    void writeLine(const QString &text);
    void handleTranscriptionFinished(const QList<TranscribeFileResult> &results);
    void finish();

    SettingsStore *m_settings;
    ProviderRegistry *m_providers;
    MicrophoneFactory m_createMicrophone;
    FileTranscriptionSession *m_transcription;
    QPointer<AudioInput> m_microphone;
    RecordingTranscript m_transcript;
    QElapsedTimer m_clock;
    RecordingStream m_stream;
    bool m_recording = false;
    bool m_stopping = false;
    // Ends a stop the provider never finishes.
    QTimer m_stopDeadline;
    // Skip silence's threshold, which says what audio is speech.
    int m_voiceThreshold = 0;
    // Runs from the latest speech until a pause ends the utterance; while it
    // runs, an utterance is open.
    QTimer m_pauseTimer;
    // Audio heard since the open utterance began.
    qsizetype m_utteranceBytes = 0;
};

} // namespace speecher

Q_DECLARE_METATYPE(speecher::RecordingStatus)
