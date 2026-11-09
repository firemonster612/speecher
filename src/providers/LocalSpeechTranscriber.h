#pragma once

#include "providers/LocalSpeechEngine.h"
#include "providers/SpeechTranscriber.h"

#include <memory>

namespace speecher {

class LocalModelStore;
class LocalSpeechWorker;
class LocalTranscriberWork;

// How one streaming update divides into text the session keeps and text it
// may still revise. Committed text can end mid-word, and each final is joined
// to the next with a space, so only whole words become final.
struct StreamSplit {
    // Newly final words; empty when none completed.
    QString finalWords;
    // Everything after the final text: the rest of committed plus tentative.
    QString partial;
    // How much of committed text is final after this update.
    qsizetype finalChars = 0;
};

// finalChars is what the previous update returned for this stream.
StreamSplit splitStreamText(const LocalSpeechEngine::StreamText &text, qsizetype finalChars);

// Speech-to-text with a Local Model, on the worker every transcriber shares:
// every call here only queues work there, and results come back as signals on
// the caller's thread. A dictation streams runs of words as they are
// committed, then the whole text at the end in their place. With
// SpeechAttemptOptions::cutIntoUtterances each utterance is one final,
// emitted at its endUtterance(), and nothing replaces them.
class LocalSpeechTranscriber final : public SpeechTranscriber {
    Q_OBJECT

public:
    LocalSpeechTranscriber(const LocalModelStore &store, std::shared_ptr<LocalSpeechWorker> worker,
                           QObject *parent = nullptr);
    ~LocalSpeechTranscriber() override;

    bool requiresRefresh(const SpeechSettings &settings, const AccountSettings &accounts) const override;
    std::optional<ProviderFailure> prepare(const SpeechSettings &settings, const AccountSettings &accounts) override;
    int utteranceAnswerTimeoutMs() const override;
    bool streamsFinalText(const SpeechSettings &settings) const override;
    void startAttempt(quint64 attemptId, const SpeechSettings &settings, const SpeechAttemptOptions &options) override;
    void sendAudio(quint64 attemptId, const QByteArray &pcm) override;
    void finishInput(quint64 attemptId) override;
    void cancelAttempt(quint64 attemptId) override;
    void endUtterance(quint64 attemptId) override;
    void beginUtterance(quint64 attemptId) override;

    // The Speed Test, on the worker and model dictation uses, so a large
    // model is never in memory twice. Refused while an attempt runs on any
    // transcriber, and stopped by one that starts.
    void runSpeedTest(const QString &modelId, const LocalRunsOn &runsOn);

signals:
    // seconds is valid when error is empty.
    void speedTestFinished(const QString &modelId, double seconds, const QString &error);
    // After every load and unload by any transcriber: where the model now
    // runs, empty for nowhere.
    void runsOnChanged(const QString &description);

private:
    // The model file for modelId, or empty with error set.
    QString downloadedModelPath(const QString &modelId, QString *error) const;
    // Whether text for attemptId still reaches the caller.
    bool isLive(quint64 attemptId) const;
    void setLiveAttempt(quint64 attemptId, int idleUnloadMinutes = 0);

    const LocalModelStore &m_store;
    std::shared_ptr<LocalSpeechWorker> m_worker;
    // Lives on the worker thread, which deletes it once the work queued
    // before has run.
    LocalTranscriberWork *m_work;
};

} // namespace speecher
