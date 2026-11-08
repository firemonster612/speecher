#pragma once

#include "core/AppSettings.h"
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
class ProviderRegistry;
class RecordingAudio;
class RecordingMicrophone;
class RecordingStream;
class RecordingSystemAudio;
class SettingsStore;

// Where transcripts go: the folder chosen, or Documents/Speecher.
QString recordingFolder(const RecordingSettings &settings);
// A recording's file in folder when the command names none: its name made
// fit for a file name, then when it started ("weekly-sync-2026-11-08-1000.md"),
// or the time alone for one without a name.
QString recordingPath(const QString &folder, const QString &title, RecordingFormat format, const QDateTime &startedAt);

// What one recording is started with; the rest comes from RecordingSettings.
struct RecordingRequest {
    // The file to write (record start --to), or empty for one in the
    // recordings folder named by title and the time.
    QString path;
    // The name the person gave it, which also names its file; may be empty.
    QString title;
    // Terms for this recording alone, ahead of the saved ones (--vocab-file).
    QStringList vocabulary;
    // record start --mic-only, over RecordingSettings::systemAudio.
    bool microphoneOnly = false;
};

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
// uploads each utterance on its own (the speech server) still writes lines
// while it runs. It starts once the microphone's stream connects and system
// audio's connects or fails; a stream that drops after that reconnects until
// the recording stops, and one that stops leaves the other recording. Once
// neither stream has heard speech for RecordingSettings::autoStopMinutes, it
// stops itself.
class RecordingSession : public QObject {
    Q_OBJECT

public:
    // Starting: from start() until both streams have settled. Stopping: from
    // stop() until the last utterances are written.
    enum class Phase { Off, Starting, Recording, Stopping };

    using InputFactory = std::function<AudioInput *(QObject *parent)>;
    // Takes why echo cancellation is off, when it is.
    using EchoCancellerFactory = std::function<std::unique_ptr<EchoCanceller>(QString *warning)>;
    // Takes why a recording could not start, or nothing once it runs.
    using StartDone = std::function<void(const QString &error)>;

    // Without createSystemAudio, or when it makes none, a recording takes the
    // microphone alone. createEchoCanceller is asked for one only beside
    // system audio; with one, the microphone is told to keep silence.
    RecordingSession(SettingsStore *settings,
                     ProviderRegistry *providers,
                     InputFactory createMicrophone,
                     InputFactory createSystemAudio = {},
                     EchoCancellerFactory createEchoCanceller = {},
                     QObject *parent = nullptr);
    ~RecordingSession() override;

    // From the start's done until stopped is emitted.
    bool isRecording() const;
    Phase phase() const;
    RecordingStatus status() const;
    // Starts recording as request asks. Calls done once, perhaps before
    // returning: when the recording runs, or with why it cannot (already
    // recording or starting, a speech provider that does not stream final
    // text, a file that cannot be created, a microphone that cannot start, no
    // provider that connects for the microphone), in which case no file is
    // left.
    void start(const RecordingRequest &request, StartDone done);
    // Stops the inputs; the last utterances are still written before stopped
    // is emitted.
    void stop();
    // Ends the recording at once and removes its file, for a start whose
    // caller stopped waiting and so took it for a failure.
    void discard();
    // While a dictation holds the microphone, the microphone's stream sends
    // silence and shows paused; system audio records on.
    void setDictating(bool dictating);
    // While a dictation listens, pauses and all. Each one that listens while
    // the recording runs, or already did when it started, writes one line
    // saying so.
    void setDictationListening(bool listening);

    // How long one minute of Stop after silence lasts, for tests.
    static void setAutoStopMinuteMs(int ms);

signals:
    void recordingChanged(bool recording);
    void phaseChanged();
    // The recording runs, writing to path; title is its name, or empty.
    void started(const QString &path, const QString &title);
    // A line the file took: when its text was finalised, from the start, who
    // said it and what, on one line.
    void lineWritten(qint64 atMs, const QString &speaker, const QString &text);
    // A stream's input level, 0 to 1, at most 20 times a second each.
    void levelChanged(const QString &speaker, float level);
    // While recording, a stream reconnects, connects again, stops on its
    // own, pauses for a dictation or resumes, misses an utterance or starts
    // losing audio, echo cancellation turns off as system audio stops, or the
    // file first fails to take a line. Not for more audio lost or more lines
    // missed, which can come with every chunk or line.
    void problemChanged();
    // The finished recording's last status.
    void stopped(const speecher::RecordingStatus &status);

private:
    // One input, its speech stream and the utterances the recording cuts.
    struct Stream {
        RecordingStream *speech = nullptr;
        RecordingStreamStatus status;
        // From the start until the stream first connects or fails.
        bool connecting = false;
        // Runs from the latest speech until a pause ends the utterance; while
        // it runs, an utterance is open.
        QTimer pauseTimer;
        // Audio heard since the open utterance began.
        qsizetype utteranceBytes = 0;
        // Since the level was last reported.
        QElapsedTimer levelReported;
    };

    void connectStream(Stream &stream);
    void startStream(Stream &stream, AudioInput *input, const AppSettings &settings);
    void handleStreamConnected(Stream &stream);
    void finishStartOnceSettled();
    void stopSystemAudio();
    void listeningFinished(Stream &stream);
    void markDictation();
    void trackUtterance(Stream &stream, const QByteArray &pcm);
    void endUtterance(Stream &stream);
    void forgetUtterance(Stream &stream);
    void extendStop();
    void writeLine(const QString &speaker, const QString &text);
    void handleStreamFinished(Stream &stream, const QString &error);
    void handleSystemAudioFailed();
    void stopCancellingEcho();
    // The state a live stream shows: the microphone's pauses for a dictation.
    RecordingStreamStatus::State liveState(const Stream &stream) const;
    void releaseInputs();
    void abandon(const QString &error);
    void finish();
    void setPhase(Phase phase);

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
    // What system audio's stream reads, when the recording takes it.
    QPointer<RecordingSystemAudio> m_systemAudio;
    // Why Speecher's echo canceller is off: it could not start, or system
    // audio stopped.
    QString m_echoCancellationWarning;
    bool m_dictating = false;
    // Whether the dictation listens, and whether its line is written.
    bool m_dictationListening = false;
    bool m_dictationMarked = false;
    RecordingTranscript m_transcript;
    QString m_title;
    // Both sides' audio, while Keep the call's audio is on.
    RecordingAudio *m_audio = nullptr;
    // Runs from the last speech either stream heard until it stops the
    // recording.
    QTimer m_silence;
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
