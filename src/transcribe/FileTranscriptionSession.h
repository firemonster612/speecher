#pragma once

#include "core/AppSettings.h"
#include "dictation/DictationPorts.h"
#include "dictation/TranscriptPipeline.h"
#include "transcribe/Subtitles.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include <functional>
#include <optional>

class QAudioDecoder;

namespace speecher {

class ProviderRegistry;
class SettingsStore;
class StartupPreparationRunner;
class TranscriptState;

enum class TranscriptDestination {
    BesideInput,
    Folder,
    None,
};

// One batch's choices. Seeded from the user's settings by the front end and
// never written back to them.
struct TranscribeOptions {
    QString speechProviderId;
    bool applyVocabulary = true;
    // Terms added to the custom vocabulary for this batch, even without
    // applyVocabulary.
    QStringList addedVocabulary;
    QString refinementProviderId = QStringLiteral("none");
    QString cleanupStrength = QStringLiteral("none");
    QString tone = QStringLiteral("none");
    QString writingProfile = WritingProfile::Other;
    // Replaces the Spoken Language setting for this batch.
    std::optional<QString> spokenLanguage;
    // A recording's: each final the provider streams is the transcript as it
    // arrives, so only speech providers that stream final text take the
    // audio, and Codex does not transcribe it again at the end.
    bool streamedFinalsOnly = false;
    TranscriptDestination destination = TranscriptDestination::BesideInput;
    QString folder;
};

struct TranscribeFileResult {
    QString path;
    // What the speech provider heard.
    QString raw;
    // When each part of raw was spoken, in milliseconds from the start of the
    // file; empty when the speech provider returned no timings.
    QList<TranscriptSegment> segments;
    // The finished transcript: the model's refinement when it ran, otherwise
    // the raw text after vocabulary corrections. This is what gets saved.
    // Empty means the file failed.
    QString refined;
    QString savedPath;
    // Why the file failed, or for a finished file what went wrong on the way
    // (refinement fell back to the raw text, or saving failed).
    QString error;

    bool failed() const { return refined.isEmpty(); }
};

// The files Speecher offers to transcribe: the file pickers and their hint
// name these extensions, and the packaging registers "Open with" for them (a
// test holds the .desktop, Info.plist and .iss lists to these). A test writes
// a clip in each video container and transcribes it, wherever the media
// backend reports it can write that clip. Dropped and opened files are not
// limited to the list; see isAudioFile.
QStringList transcribableExtensions();
// The MIME types of those files, with the aliases older shared-mime-info
// releases and file managers still use.
QStringList transcribableMimeTypes();

// True for any audio or video file, by its content or name, so a dropped or
// opened file the pickers do not list still gets its try with the decoder.
bool isAudioFile(const QString &path);

// Reads an audio file's length in the background and hands it to done, in
// milliseconds, on receiver's thread. done is never called for a file that
// cannot be read, or once receiver is gone.
void probeAudioDuration(const QString &path, QObject *receiver, std::function<void(qint64)> done);

// Writes text as "<name>-transcribed.txt" in folder, or .srt or .vtt for
// subtitles, numbering it "<name>-transcribed (2).txt" and so on rather than
// overwrite a file. Returns the path written, or empty with error set.
QString saveTranscript(const QString &audioPath,
                       const QString &folder,
                       const QString &text,
                       TranscriptFormat format,
                       QString *error);

// Transcribes audio files one after another, or what a microphone hears,
// with fresh provider instances, so a batch never shares a transcriber or
// refiner with live dictation.
class FileTranscriptionSession : public QObject {
    Q_OBJECT

public:
    FileTranscriptionSession(SettingsStore *settings, ProviderRegistry *providers, QObject *parent = nullptr);
    ~FileTranscriptionSession() override;

    bool isRunning() const;
    // False when a batch is already running or there is nothing to do.
    bool start(const QStringList &paths, const TranscribeOptions &options);
    // Transcribes what input hears until finishListening(), as one file with
    // no path that is never saved. Audio heard while the providers prepare waits for them, and a
    // provider that fails before any reached it hands all of it to the next.
    // False when a batch is already running.
    bool startListening(AudioInput *input, const TranscribeOptions &options);
    // Stops the microphone; what it heard is still transcribed and refined.
    void finishListening();
    // Stops the current file, skips the rest and still emits batchFinished.
    void cancel();

signals:
    void batchStarted(int count);
    void fileStarted(int index, const QString &path);
    // The decoded file: peak levels (0..1) across its length, for drawing.
    void fileDecoded(int index, const QVector<float> &peaks, qint64 durationMs);
    void fileProgress(int index, qreal fractionOfAudioSent);
    void filePartialText(int index, const QString &text);
    // Each piece of text that will not change again, in order: a final the
    // provider sent, or the partial a stream left when it ended. A
    // whole-attempt transcript, which replaces them, is not one.
    void fileTextFinalized(int index, const QString &text);
    void fileRefining(int index);
    void fileFinished(int index, const speecher::TranscribeFileResult &result);
    // Every file that finished or failed; a cancelled one is left out.
    void batchFinished(const QList<speecher::TranscribeFileResult> &results, bool cancelled);

private:
    // Where the microphone is, for a batch started with startListening().
    enum class Microphone { Off, Listening, Stopping };

    void beginBatch(const QStringList &paths, const TranscribeOptions &options);
    void startFile();
    void startMicrophone();
    void handleDecodedBuffer();
    void handleDecodeFinished();
    void prepareProviders();
    void prepareSpeechProvider();
    void connectTranscriber();
    void releaseTranscriber();
    void beginStreaming();
    void sendNextChunk();
    void startNextAttempt();
    void handleAttemptCompleted(quint64 attemptId);
    void handleSpeechFailure(const SpeechFailure &failure);
    QString speechFailureText(const QString &providerMessage) const;
    bool attemptWasStable() const;
    void finishTranscription();
    void refine(const QString &raw);
    void completeFile(const QString &text);
    void failFile(const QString &message);
    void finishFile();
    void releaseFileResources();
    void commitFinal(const QString &text);
    void dropSentMicrophoneAudio();

    SettingsStore *m_settings;
    ProviderRegistry *m_providers;
    StartupPreparationRunner *m_preparation;
    TranscriptState *m_transcript;
    QTimer m_sendTimer;
    QStringList m_paths;
    TranscribeOptions m_options;
    // The user's settings with this batch's choices applied.
    AppSettings m_batchSettings;
    QList<TranscribeFileResult> m_results;
    TranscribeFileResult m_current;
    bool m_running = false;
    int m_index = -1;

    QPointer<QAudioDecoder> m_decoder;
    // The microphone a startListening() batch reads instead of a file.
    QPointer<AudioInput> m_input;
    Microphone m_microphone = Microphone::Off;
    QPointer<SpeechTranscriber> m_transcriber;
    QPointer<TranscriptRefiner> m_refiner;
    QByteArray m_pcm;
    // Microphone audio dropped from the front of m_pcm once sent and no other
    // provider can need it, so a long recording does not keep all it heard.
    qsizetype m_pcmDropped = 0;
    // Bytes sent, counted from the start of the input.
    qsizetype m_sent = 0;
    // The current provider has sent text, so the audio before m_sent reached
    // it and will not be sent again.
    bool m_heardFromProvider = false;
    bool m_inputFinished = false;
    // An attempt is open for the audio; false while a provider prepares.
    bool m_streaming = false;
    quint64 m_attemptId = 0;
    quint64 m_preparationRevision = 0;
    int m_reconnectsLeft = 0;
    QElapsedTimer m_attemptClock;
    // Where in the file the current attempt's audio starts.
    qint64 m_attemptStartMs = 0;
    // Text committed before the current attempt; a whole-attempt transcript
    // replaces only what followed it.
    QString m_attemptBaseText;
    TranscriptPipelineResult m_pipeline;
    // The page's speech provider, then the saved fallbacks; each file walks
    // it from the start, and says why each one it passed could not help.
    QStringList m_speechChain;
    int m_speechIndex = 0;
    QList<ProviderAttemptIssue> m_speechIssues;
    // The refiner's sign-in renewed for this file: once, not for each
    // speech provider tried.
    bool m_refinerRefreshed = false;
};

} // namespace speecher

Q_DECLARE_METATYPE(speecher::TranscribeFileResult)
