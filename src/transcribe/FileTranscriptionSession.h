#pragma once

#include "core/AppSettings.h"
#include "dictation/DictationPorts.h"
#include "dictation/TranscriptPipeline.h"

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
    QString refinementProviderId = QStringLiteral("none");
    QString cleanupStrength = QStringLiteral("none");
    QString tone = QStringLiteral("none");
    QString writingProfile = QStringLiteral("other");
    TranscriptDestination destination = TranscriptDestination::BesideInput;
    QString folder;
};

struct TranscribeFileResult {
    QString path;
    // What the speech provider heard.
    QString raw;
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

// True for a file the decoder can take: audio, or video whose audio track it
// reads (shared-mime-info files audio-only .webm and .mp4 under video/).
bool isAudioFile(const QString &path);

// Reads an audio file's length in the background and hands it to done, in
// milliseconds, on receiver's thread. done is never called for a file that
// cannot be read, or once receiver is gone.
void probeAudioDuration(const QString &path, QObject *receiver, std::function<void(qint64)> done);

// Writes text as "<name>-transcribed.txt" in folder, numbering it
// "<name>-transcribed (2).txt" and so on rather than overwrite a file.
// Returns the path written, or empty with error set.
QString saveTranscript(const QString &audioPath, const QString &folder, const QString &text, QString *error);

// Transcribes audio files one after another with fresh provider instances,
// so a batch never shares a transcriber or refiner with live dictation.
class FileTranscriptionSession : public QObject {
    Q_OBJECT

public:
    FileTranscriptionSession(SettingsStore *settings, ProviderRegistry *providers, QObject *parent = nullptr);
    ~FileTranscriptionSession() override;

    bool isRunning() const;
    // False when a batch is already running or there is nothing to do.
    bool start(const QStringList &paths, const TranscribeOptions &options);
    // Stops the current file, skips the rest and still emits batchFinished.
    void cancel();

signals:
    void batchStarted(int count);
    void fileStarted(int index, const QString &path);
    // The decoded file: peak levels (0..1) across its length, for drawing.
    void fileDecoded(int index, const QVector<float> &peaks, qint64 durationMs);
    void fileProgress(int index, qreal fractionOfAudioSent);
    void filePartialText(int index, const QString &text);
    void fileRefining(int index);
    void fileFinished(int index, const speecher::TranscribeFileResult &result);
    // Every file that finished or failed; a cancelled one is left out.
    void batchFinished(const QList<speecher::TranscribeFileResult> &results, bool cancelled);

private:
    void startFile();
    void handleDecodedBuffer();
    void handleDecodeFinished();
    void prepareProviders();
    void beginStreaming();
    void sendNextChunk();
    void startNextAttempt();
    void handleAttemptCompleted(quint64 attemptId);
    void handleSpeechFailure(const SpeechFailure &failure);
    bool attemptWasStable() const;
    void finishTranscription();
    void refine(const QString &raw);
    void completeFile(const QString &text);
    void failFile(const QString &message);
    void finishFile();
    void releaseFileResources();

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
    QPointer<SpeechTranscriber> m_transcriber;
    QPointer<TranscriptRefiner> m_refiner;
    QByteArray m_pcm;
    qsizetype m_sent = 0;
    bool m_inputFinished = false;
    quint64 m_attemptId = 0;
    quint64 m_preparationGeneration = 0;
    int m_reconnectsLeft = 0;
    QElapsedTimer m_attemptClock;
    // Text committed before the current attempt; a whole-attempt transcript
    // replaces only what followed it.
    QString m_attemptBaseText;
    TranscriptPipelineResult m_pipeline;
};

} // namespace speecher

Q_DECLARE_METATYPE(speecher::TranscribeFileResult)
