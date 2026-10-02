#pragma once

#include "dictation/DictationPorts.h"
#include "providers/LocalSpeechEngine.h"

#include <QMutex>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <functional>

namespace speecher {

class LocalModelStore;

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

// Speech-to-text with a Local Model. Inference runs on a thread of its own:
// every call here only queues work there, and results come back as signals on
// the caller's thread.
class LocalSpeechTranscriber final : public SpeechTranscriber {
    Q_OBJECT

public:
    LocalSpeechTranscriber(const LocalModelStore &store, QObject *parent = nullptr);
    ~LocalSpeechTranscriber() override;

    QString id() const override;
    QString label() const override;
    bool requiresRefresh(const SpeechSettings &settings) const override;
    SpeechPrepareResult prepare(const SpeechSettings &settings) override;
    void startAttempt(quint64 attemptId, const SpeechSettings &settings) override;
    void sendAudio(quint64 attemptId, const QByteArray &pcm) override;
    void finishInput(quint64 attemptId) override;
    void cancelAttempt(quint64 attemptId) override;

    // The Speed Test, on the same thread and engine dictation uses, so a large
    // model is never in memory twice.
    void runSpeedTest(const QString &modelId, const LocalRunsOn &runsOn);

signals:
    // seconds is valid when error is empty.
    void speedTestFinished(const QString &modelId, double seconds, const QString &error);
    // After every load and unload: where the model now runs, empty for nowhere.
    void runsOnChanged(const QString &description);

private:
    // The model file for modelId, or empty with error set.
    QString downloadedModelPath(const QString &modelId, QString *error) const;
    void onWorker(std::function<void()> work);
    void onCaller(std::function<void()> work);
    void report(quint64 attemptId, std::function<void()> emitSignal);
    void reportEnd(quint64 attemptId, std::function<void()> emitSignal);
    void startIdleTimer();

    // Worker thread only.
    bool ensureLoaded(const QString &modelPath, const LocalRunsOn &runsOn, QString *error);
    void announceRunsOn();
    void begin(quint64 attemptId, const QString &modelPath, const LocalRunsOn &runsOn, bool timed);
    void feedPending();
    void finish(quint64 attemptId);
    bool attemptRunning(quint64 attemptId) const;
    void failAttempt(quint64 attemptId, const QString &message, const QString &phase);

    const LocalModelStore &m_store;
    // The attempt the caller still wants results for, 0 for none. The worker
    // reads it to abandon work, and the engine to abort a running call.
    std::atomic<quint64> m_liveAttempt = 0;
    // Set on destruction so a running Speed Test stops too, at the next point
    // transcribe.cpp checks: between chunks and decode steps, not mid-encoder.
    std::atomic_bool m_shuttingDown = false;
    QTimer m_idleTimer;
    // Audio for m_liveAttempt the worker has not taken yet. When inference
    // falls behind the microphone, the worker feeds everything that piled up
    // at once instead of working through a backlog of small chunks.
    QMutex m_pendingMutex;
    QByteArray m_pendingPcm;
    bool m_feedQueued = false;

    QThread m_thread;
    // Lives on m_thread; destroyed only after the thread has stopped.
    QObject m_workerContext;
    // Everything below belongs to the worker thread.
    LocalSpeechEngine m_engine;
    quint64 m_workerAttempt = 0;
    bool m_workerAttemptFailed = false;
    // Whether the attempt asked for segment timings.
    bool m_workerAttemptTimed = false;
    QByteArray m_batchPcm;
    qsizetype m_emittedCommittedChars = 0;
};

} // namespace speecher
