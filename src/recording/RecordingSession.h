#pragma once

#include "recording/RecordingStatus.h"

#include <QDateTime>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QFile>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <functional>
#include <memory>

namespace speecher {

class AudioInput;
class EchoCanceller;
class FileTranscriptionSession;
class ProviderRegistry;
class RecordingMicrophone;
class SettingsStore;
struct TranscribeFileResult;
struct TranscribeOptions;

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

// Records a call into a transcript file until stop(), with its own inputs
// and providers, beside any dictation: the microphone as "me" and, where the
// platform captures it, system audio as "them", each with its own speech
// stream, their lines appended to the one file as they come. Each final text
// a speech provider streams becomes a line, timed from the recording's start;
// partials are never written, except the one a dropped stream leaves. A line
// carries the time its text was finalised, as streamed finals come with no
// timings, so the two streams' lines can arrive slightly out of order. The
// recording ends an utterance itself at a pause or once it runs long, and
// again at a stop, so a provider that finalizes only when asked (Codex) or
// uploads each utterance on its own (the Custom Endpoint) still writes lines
// while it runs. It starts once the microphone's stream connects and system
// audio's connects or fails; a stream that drops after that reconnects until
// the recording stops, and one that stops leaves the other recording.
class RecordingSession : public QObject {
    Q_OBJECT

public:
    using InputFactory = std::function<AudioInput *(QObject *parent)>;
    // Takes why echo cancellation is off, when it is.
    using EchoCancellerFactory = std::function<std::unique_ptr<EchoCanceller>(QString *warning)>;
    // Takes why a recording could not start, or nothing once it runs.
    using StartDone = std::function<void(const QString &error)>;

    // Without createSystemAudio, or when it makes none, a recording takes the
    // microphone alone. createEchoCanceller is asked for one only beside
    // system audio; with it the microphone must not skip silence.
    RecordingSession(SettingsStore *settings,
                     ProviderRegistry *providers,
                     InputFactory createMicrophone,
                     InputFactory createSystemAudio = {},
                     EchoCancellerFactory createEchoCanceller = {},
                     QObject *parent = nullptr);
    ~RecordingSession() override;

    // From the start's done until stopped is emitted.
    bool isRecording() const;
    RecordingStatus status() const;
    // Starts recording into path, or the default file in dataFolder when path
    // is empty, with vocabulary added to the custom vocabulary, from the
    // microphone alone when microphoneOnly. Calls done once, perhaps before
    // returning: when the recording runs, or with why it cannot (already
    // recording or starting, a speech provider that does not stream final
    // text, a file that cannot be created, a microphone that cannot start, no
    // provider that connects for the microphone), in which case no file is
    // left.
    void start(const QString &path,
               const QStringList &vocabulary,
               const QString &dataFolder,
               bool microphoneOnly,
               StartDone done);
    // Stops the inputs; the last utterances are still written before stopped
    // is emitted.
    void stop();
    // Ends the recording at once and removes its file, for a start whose
    // caller stopped waiting and so took it for a failure.
    void discard();
    // While a dictation has the microphone, the microphone's stream sends
    // silence, and each dictation writes a line saying so; system audio
    // records on.
    void setDictating(bool dictating);

signals:
    void recordingChanged(bool recording);
    // While recording, a stream reconnects, connects again, stops on its
    // own, pauses for a dictation or resumes, misses an utterance or starts
    // losing audio, or the file first fails to take a line. Not for more
    // audio lost or more lines missed, which can come with every chunk or
    // line.
    void problemChanged();
    // The finished recording's last status.
    void stopped(const speecher::RecordingStatus &status);

private:
    enum class Phase { Off, Starting, Recording, Stopping };

    // One input, its speech stream and the utterances the recording cuts.
    struct Stream {
        FileTranscriptionSession *transcription = nullptr;
        QPointer<AudioInput> input;
        RecordingStream status;
        // Whether this recording takes the input at all.
        bool used = false;
        // From the start until the stream first connects or fails.
        bool connecting = false;
        // Runs from the latest speech until a pause ends the utterance; while
        // it runs, an utterance is open.
        QTimer pauseTimer;
        // Audio heard since the open utterance began.
        qsizetype utteranceBytes = 0;
    };

    void connectStream(Stream &stream);
    void startStream(Stream &stream, const TranscribeOptions &options);
    void handleStreamConnected(Stream &stream);
    void finishStartOnceSettled();
    void trackUtterance(Stream &stream, const QByteArray &pcm);
    void endUtterance(Stream &stream);
    void extendStop();
    void writeLine(const QString &speaker, const QString &text);
    void handleTranscriptionFinished(Stream &stream, const QList<TranscribeFileResult> &results);
    // The state a live stream shows: the microphone's pauses for a dictation.
    RecordingStream::State liveState(const Stream &stream) const;
    void releaseInputs();
    void abandon(const QString &error);
    void finish();

    SettingsStore *m_settings;
    ProviderRegistry *m_providers;
    InputFactory m_createMicrophone;
    InputFactory m_createSystemAudio;
    EchoCancellerFactory m_createEchoCanceller;
    // The pending start's done.
    StartDone m_startDone;
    Stream m_me;
    Stream m_them;
    // What the microphone's stream reads.
    QPointer<RecordingMicrophone> m_microphone;
    QString m_echoCancellationWarning;
    bool m_dictating = false;
    RecordingTranscript m_transcript;
    QElapsedTimer m_clock;
    Phase m_phase = Phase::Off;
    // Ends a stop the provider never finishes.
    QTimer m_stopDeadline;
    // When a stop gives up on the provider, however much it still answers.
    QDeadlineTimer m_stopLimit;
    // How long a stop waits for each utterance left, as long as the provider
    // may take to answer one.
    int m_utteranceAnswerTimeoutMs = 0;
    // Skip silence's threshold, which says what audio is speech.
    int m_voiceThreshold = 0;
    // Lines the file did not take, and why the first did not.
    int m_unwrittenLines = 0;
    QString m_writeError;
};

} // namespace speecher
