#pragma once

#include "recording/RecordingStatus.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
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

// Records the microphone into a transcript file until stop(), with its own
// microphone input and providers, beside any dictation. Each final text the
// speech provider streams becomes a line, timed from the recording's start;
// partials are never written, except the one a dropped stream leaves. A line
// carries the time its text was finalised, as streamed finals come with no
// timings. The recording ends an utterance itself at a pause or once it runs
// long, so a provider that finalizes only when asked (Codex) still writes
// lines while it runs. It starts once the provider's stream connects; a
// stream that drops after that reconnects until the recording stops.
class RecordingSession : public QObject {
    Q_OBJECT

public:
    using MicrophoneFactory = std::function<AudioInput *(QObject *parent)>;
    // Takes why a recording could not start, or nothing once it runs.
    using StartDone = std::function<void(const QString &error)>;

    RecordingSession(SettingsStore *settings,
                     ProviderRegistry *providers,
                     MicrophoneFactory createMicrophone,
                     QObject *parent = nullptr);
    ~RecordingSession() override;

    // From the start's done until stopped is emitted.
    bool isRecording() const;
    RecordingStatus status() const;
    // Starts recording into path, or the default file in dataFolder when path
    // is empty, with vocabulary added to the custom vocabulary. Calls done
    // once, perhaps before returning: when the provider's stream connects and
    // the recording runs, or with why it cannot (already recording or
    // starting, a speech provider that does not stream final text, a file
    // that cannot be created, a microphone that cannot start, no provider
    // that connects), in which case no file is left.
    void start(const QString &path, const QStringList &vocabulary, const QString &dataFolder, StartDone done);
    // Stops the microphone; the last utterance is still written before
    // stopped is emitted.
    void stop();
    // Ends the recording at once and removes its file, for a start whose
    // caller stopped waiting and so took it for a failure.
    void discard();

signals:
    void recordingChanged(bool recording);
    // While recording, a stream reconnects, connects again, stops on its own
    // or loses audio.
    void streamChanged();
    // The finished recording's last status.
    void stopped(const speecher::RecordingStatus &status);

private:
    enum class Phase { Off, Starting, Recording, Stopping };

    void handleStreamConnected();
    void trackUtterance(const QByteArray &pcm);
    void endUtterance();
    void writeLine(const QString &text);
    void handleTranscriptionFinished(const QList<TranscribeFileResult> &results);
    void abandon(const QString &error);
    void finish();

    SettingsStore *m_settings;
    ProviderRegistry *m_providers;
    MicrophoneFactory m_createMicrophone;
    // The pending start's done.
    StartDone m_startDone;
    FileTranscriptionSession *m_transcription;
    QPointer<AudioInput> m_microphone;
    RecordingTranscript m_transcript;
    QElapsedTimer m_clock;
    RecordingStream m_stream;
    Phase m_phase = Phase::Off;
    // Ends a stop the provider never finishes.
    QTimer m_stopDeadline;
    // Skip silence's threshold, which says what audio is speech.
    int m_voiceThreshold = 0;
    // Runs from the latest speech until a pause ends the utterance; while it
    // runs, an utterance is open.
    QTimer m_pauseTimer;
    // Audio heard since the open utterance began.
    qsizetype m_utteranceBytes = 0;
    // Lines the file did not take, and why the first did not.
    int m_unwrittenLines = 0;
    QString m_writeError;
};

} // namespace speecher
