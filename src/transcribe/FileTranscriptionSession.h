#pragma once

#include "core/AppSettings.h"
#include "core/ProviderTable.h"
#include "dictation/DictationPorts.h"
#include "dictation/TranscriptPipeline.h"
#include "platform/audio/AudioPcmConverter.h"
#include "providers/RefinementChain.h"
#include "providers/SpeechStream.h"
#include "providers/TranscriptRefiner.h"
#include "transcribe/Subtitles.h"

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVector>

#include <functional>
#include <optional>
#include <utility>

class QAudioDecoder;

namespace speecher {

class ProviderRegistry;
class SettingsStore;
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
    QString refinementProviderId = kNoRefinementProvider;
    QString cleanupStrength = QStringLiteral("none");
    QString tone = QStringLiteral("none");
    QString writingProfile = WritingProfile::Other;
    // Replaces the Spoken Language setting for this batch.
    std::optional<QString> spokenLanguage;
    // Replace the models the settings or the profile give the chosen
    // services, for this batch; empty keeps them. They apply to the services
    // that have a model to choose: a Local Model's id or a Custom Endpoint's
    // model for speech, and see setRefinementModel for refinement.
    QString speechModel;
    QString refinementModel;
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

// settings as a batch with these options runs them: its spoken language,
// and its speech and refinement providers, each with the profile's model and
// chain where the batch kept the profile's provider, or the batch's own model.
AppSettings withBatchChoices(AppSettings settings, const ProviderRegistry &providers, const TranscribeOptions &options);

// Writes text to path, replacing what is there. Returns why it could not, or
// empty.
QString writeTranscript(const QString &path, const QString &text);

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
    void fileStarted(int index, const QString &path);
    // The decoded file: peak levels (0..1) across its length, for drawing.
    void fileDecoded(int index, const QVector<float> &peaks, qint64 durationMs);
    void fileProgress(int index, qreal fractionOfAudioSent);
    void filePartialText(int index, const QString &text);
    // From when the file's refiners start preparing.
    void fileRefining(int index);
    void fileFinished(int index, const speecher::TranscribeFileResult &result);
    // Every file that finished or failed; a cancelled one is left out.
    void batchFinished(const QList<speecher::TranscribeFileResult> &results, bool cancelled);

private:
    // Where the microphone is, for a batch started with startListening().
    enum class Microphone { Off, Listening, Stopping };

    void beginBatch(const QStringList &paths, const TranscribeOptions &options);
    void startFile();
    void measureFile();
    void streamFile();
    bool decodeFile(std::function<void(const QByteArray &)> takeAudio, std::function<void()> finished);
    void reportProgress(qint64 bytesSent);
    void readDecodedAudio();
    void releaseDecoder();
    void startMicrophone();
    void finishTranscription();
    void refine(const QString &raw);
    TranscriptRefiner *acquireRefiner(const QString &providerId);
    void releaseRefiner();
    void startRefiner();
    void handleRefinementFailure(const ProviderFailure &failure);
    void completeUnrefined();
    void completeFile(const QString &text);
    void failFile(const QString &message);
    void finishFile();
    void releaseFileResources();

    SettingsStore *m_settings;
    ProviderRegistry *m_providers;
    TranscriptState *m_transcript;
    // The file's audio, to the page's speech provider, then the saved
    // fallbacks; each file walks them from the start, as a dictation starts,
    // and stays with the first that its audio reached.
    SpeechStream *m_stream;
    // The page's refiner, then the saved fallbacks; each file walks it from
    // the start.
    RefinementChain *m_refinementChain;
    QStringList m_paths;
    TranscribeOptions m_options;
    // The user's settings with this batch's choices applied.
    AppSettings m_batchSettings;
    RunChoices m_runChoices;
    QList<TranscribeFileResult> m_results;
    TranscribeFileResult m_current;
    bool m_running = false;
    // Moves on as a batch begins or is cancelled. A handler of the batch's
    // signals, or an event loop the microphone spins, may do either; the
    // batch it interrupted goes no further.
    quint64 m_batch = 0;
    int m_index = -1;

    // The current pass over the file: a first that measures it, then one
    // that streams it.
    QPointer<QAudioDecoder> m_decoder;
    AudioPcmConverter m_converter;
    QAudioFormat m_decodedFormat;
    // Where the pass puts each buffer, as 16 kHz mono.
    std::function<void(const QByteArray &)> m_takeDecodedAudio;
    // The file's audio as 16 kHz mono: what the first pass counted, until
    // the second has decoded all of it.
    qint64 m_fileBytes = 0;
    // What the second pass has decoded so far, and whether that is all.
    qint64 m_streamedBytes = 0;
    bool m_streamDecoded = false;
    // The microphone a startListening() batch reads instead of a file.
    QPointer<AudioInput> m_input;
    Microphone m_microphone = Microphone::Off;
    QPointer<TranscriptRefiner> m_refiner;
    // Each refiner the file tried, and why it could not refine it.
    QList<std::pair<QString, QString>> m_refinementFailures;
    TranscriptPipelineResult m_pipeline;
};

} // namespace speecher

Q_DECLARE_METATYPE(speecher::TranscribeFileResult)
