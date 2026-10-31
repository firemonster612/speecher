#pragma once

#include "dictation/DictationPorts.h"
#include "providers/ProviderRegistry.h"
#include "providers/SpeechTranscriber.h"
#include "providers/TranscriptRefiner.h"
#include <QThread>
#include <QtEndian>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

namespace speecher::test {

// A PCM WAV file: 16-bit samples, interleaved when there are several channels.
inline QByteArray wavBytes(const QByteArray &samples, int sampleRate, int channels)
{
    const auto le32 = [](quint32 v) { QByteArray b(4, 0); qToLittleEndian(v, b.data()); return b; };
    const auto le16 = [](quint16 v) { QByteArray b(2, 0); qToLittleEndian(v, b.data()); return b; };
    constexpr quint16 pcmFormat = 1;
    constexpr quint16 bitsPerSample = 16;
    const quint16 blockAlign = quint16(channels * bitsPerSample / 8);
    return "RIFF" + le32(quint32(36 + samples.size())) + "WAVE"
        + "fmt " + le32(16) + le16(pcmFormat) + le16(quint16(channels)) + le32(quint32(sampleRate))
        + le32(quint32(sampleRate) * blockAlign) + le16(blockAlign) + le16(bitsPerSample)
        + "data" + le32(quint32(samples.size())) + samples;
}

class FakeAudioInput final : public AudioInput {
public:
    explicit FakeAudioInput(QObject *parent = nullptr)
        : AudioInput(parent)
    {
    }

    bool start(QString *error = nullptr) override
    {
        if (!startResult) {
            if (error) {
                *error = startError;
            }
            return false;
        }
        started = true;
        active = true;
        if (onStart) {
            onStart();
        }
        if (!startResult) {
            active = false;
        }
        return startResult;
    }

    void stop() override
    {
        if (onStop) {
            onStop();
        }
        active = false;
        emit levelChanged(0.0f);
    }

    bool isActive() const override
    {
        return active;
    }

    void useDevice(const QString &deviceId) override
    {
        device = deviceId;
    }

    QString echoCancellationWarning() const override
    {
        return echoWarning;
    }

    void keepSilence() override
    {
        keepsSilence = true;
    }

    void pushAudio(const QByteArray &pcm)
    {
        emit audioChunk(pcm);
    }

    void emitFailure(const QString &message)
    {
        emit failed(message);
    }

    bool startResult = true;
    QString startError = QStringLiteral("audio failed");
    QString echoWarning;
    QString device;
    bool started = false;
    bool active = false;
    bool keepsSilence = false;
    std::function<void()> onStart;
    std::function<void()> onStop;
};

class FakeMediaController final : public MediaController {
public:
    explicit FakeMediaController(QObject *parent = nullptr)
        : MediaController(parent)
    {
    }

    void pausePlaying() override
    {
        ++pauseCalls;
    }

    void resumePaused() override
    {
        ++resumeCalls;
    }

    int pauseCalls = 0;
    int resumeCalls = 0;
};

class FakeTargetProvider final : public TargetProvider {
public:
    using TargetProvider::TargetProvider;

    Target capture() override
    {
        ++captureCalls;
        if (onCapture) {
            onCapture();
        }
        return target;
    }

    // Delivery asks twice: once before it copies, and again immediately
    // before the keystrokes go out. focusedAtPasteTime is the window that
    // moved in between.
    bool stillFocused(const Target &) override
    {
        ++focusChecks;
        return focusChecks == 1 ? focused : focusedAtPasteTime;
    }

    bool preparePaste(const Target &) override { return pasteReady; }

    bool verifyInsertion(const Target &, const QString &) override
    {
        return verified;
    }

    bool canInsertText(const Target &) override
    {
        return directInsertionAvailable;
    }

    bool insertText(const Target &, const QString &text, QString *) override
    {
        ++insertCalls;
        insertedText = text;
        return inserted;
    }

    Target target;
    int captureCalls = 0;
    int insertCalls = 0;
    bool focused = true;
    bool focusedAtPasteTime = true;
    int focusChecks = 0;
    bool pasteReady = true;
    bool verified = false;
    bool directInsertionAvailable = false;
    bool inserted = false;
    QString insertedText;
    // Runs inside capture(), as events delivered in a real provider's nested
    // event loop would.
    std::function<void()> onCapture;
};

class FakeScreenshotContextProvider final : public ScreenshotContextProvider {
public:
    using ScreenshotContextProvider::ScreenshotContextProvider;

    void capture() override
    {
        ++captureCalls;
        if (autoComplete) {
            emit captured(data, mediaType);
        }
    }

    void cancel() override
    {
        ++cancelCalls;
    }

    QByteArray data = QByteArrayLiteral("screenshot-bytes");
    QString mediaType = QStringLiteral("image/png");
    bool autoComplete = true;
    int captureCalls = 0;
    int cancelCalls = 0;
};

class FakeSpeechTranscriber final : public SpeechTranscriber {
public:
    explicit FakeSpeechTranscriber(QObject *parent = nullptr)
        : SpeechTranscriber(parent)
    {
    }

    bool requiresRefresh(const SpeechSettings &, const AccountSettings &) const override
    {
        return refreshRequired;
    }

    bool renewsSignIn() const override
    {
        return true;
    }

    int utteranceAnswerTimeoutMs() const override
    {
        return utteranceAnswerTimeout.value_or(SpeechTranscriber::utteranceAnswerTimeoutMs());
    }

    std::optional<PreparationJob> createPrepareJob(const SpeechSettings &, const AccountSettings &) override
    {
        if (!backgroundPrepare) {
            return std::nullopt;
        }

        PreparationJob job;
        job.showRefreshIndicator = refreshRequired;
        // The worker may outlive the transcriber, which letting go of the
        // provider deletes, so it takes copies and counts on state the two
        // share.
        job.run = [prepares = m_backgroundPrepares, during = duringBackgroundPrepare,
                   delayMs = backgroundPrepareDelayMs, failure = prepareFailure] {
            ++prepares->started;
            if (during) {
                during();
            }
            QThread::msleep(delayMs);
            ++prepares->finished;
            return failure;
        };
        job.apply = [this](const std::optional<ProviderFailure> &) {
            ++prepareCalls;
        };
        return job;
    }

    std::optional<ProviderFailure> prepare(const SpeechSettings &, const AccountSettings &) override
    {
        ++prepareCalls;
        return prepareFailure;
    }

    bool streamsFinalText(const SpeechSettings &) const override
    {
        return streamsFinals;
    }

    void startAttempt(quint64 attemptId, const SpeechSettings &settings, const SpeechAttemptOptions &) override
    {
        ++startCalls;
        currentAttemptId = attemptId;
        lastVocabulary = settings.vocabulary;
        lastLanguage = settings.language;
        lastLocalModel = settings.local.modelId;
        if (onStartAttempt) {
            onStartAttempt();
        }
    }

    void sendAudio(quint64 attemptId, const QByteArray &pcm) override
    {
        if (ignoresEndedAttempts && (endedAttempts.contains(attemptId) || inputEndedAttempts.contains(attemptId))) {
            return;
        }
        if (attemptId == currentAttemptId) {
            audioChunks << pcm;
        }
    }

    void finishInput(quint64 attemptId) override
    {
        ++stopCalls;
        if (ignoresEndedAttempts && endedAttempts.contains(attemptId)) {
            return;
        }
        inputEndedAttempts.append(attemptId);
        if (onFinishInput) {
            onFinishInput();
        }
        if (autoCompleteOnFinish) {
            endedAttempts.append(attemptId);
            emit attemptCompleted(attemptId);
        }
    }

    void cancelAttempt(quint64 attemptId) override
    {
        cancelledAttempts.append(attemptId);
        if (onCancelAttempt) {
            onCancelAttempt(attemptId);
        }
    }

    void endUtterance(quint64 attemptId) override
    {
        if (attemptId == currentAttemptId && onEndUtterance) {
            onEndUtterance();
        }
    }

    void beginUtterance(quint64 attemptId) override
    {
        if (attemptId == currentAttemptId) {
            ++beginUtteranceCalls;
        }
    }

    void emitConnected()
    {
        emit attemptConnected(currentAttemptId);
    }

    void emitPartialText(const QString &text)
    {
        emit partialTranscript(currentAttemptId, text);
    }

    void emitFinalText(const QString &text)
    {
        emit finalTranscript(currentAttemptId, text);
    }

    void emitAttemptText(const QString &text)
    {
        emit attemptTranscript(currentAttemptId, text);
    }

    void emitFailure(const QString &message,
                     bool retryable = false,
                     SpeechPhase phase = SpeechPhase::Streaming,
                     ProviderFailureKind kind = ProviderFailureKind::Other)
    {
        emit failed({currentAttemptId, message, retryable, phase, kind});
    }

    void emitCompletion()
    {
        endedAttempts.append(currentAttemptId);
        emit attemptCompleted(currentAttemptId);
    }

    QString providerId = QStringLiteral("claude");
    bool refreshRequired = false;
    bool backgroundPrepare = false;
    unsigned long backgroundPrepareDelayMs = 0;
    std::optional<ProviderFailure> prepareFailure;
    bool streamsFinals = false;
    std::optional<int> utteranceAnswerTimeout;
    // Runs inside a background prepare job, on the worker.
    std::function<void()> duringBackgroundPrepare;
    // Runs inside startAttempt(), as a provider that fails at once would.
    std::function<void()> onStartAttempt;
    // Runs inside finishInput(), as a provider that sends its last words as
    // it finishes.
    std::function<void()> onFinishInput;
    // Runs inside cancelAttempt(), as a provider that emits while it stops.
    std::function<void(quint64)> onCancelAttempt;
    // Runs inside endUtterance(), as a provider that finalizes when asked.
    std::function<void()> onEndUtterance;
    // Background prepare jobs started, and run to the end, on the worker.
    int backgroundPrepareCalls() const
    {
        return m_backgroundPrepares->started;
    }
    int backgroundPreparesFinished() const
    {
        return m_backgroundPrepares->finished;
    }
    int prepareCalls = 0;
    int startCalls = 0;
    int stopCalls = 0;
    int beginUtteranceCalls = 0;
    quint64 currentAttemptId = 0;
    bool autoCompleteOnFinish = true;
    // As a real provider: finishInput() for an attempt that has ended, and
    // audio for one that ended or was told its input ended, do nothing.
    bool ignoresEndedAttempts = false;
    QList<quint64> endedAttempts;
    QList<quint64> inputEndedAttempts;
    QList<quint64> cancelledAttempts;
    QList<QByteArray> audioChunks;
    QStringList lastVocabulary;
    QString lastLanguage;
    QString lastLocalModel;

private:
    struct BackgroundPrepares {
        std::atomic_int started = 0;
        std::atomic_int finished = 0;
    };
    std::shared_ptr<BackgroundPrepares> m_backgroundPrepares = std::make_shared<BackgroundPrepares>();
};

class FakeRefiner final : public TranscriptRefiner {
public:
    explicit FakeRefiner(QObject *parent = nullptr)
        : TranscriptRefiner(parent)
    {
    }

    std::optional<PreparationJob> createRefreshJob(const RefinementSettings &, const AccountSettings &) override
    {
        if (!refreshRequired) {
            return std::nullopt;
        }

        PreparationJob job;
        job.showRefreshIndicator = true;
        // The worker may outlive the refiner, which a cancel deletes, so it
        // takes copies and counts on state the two share.
        job.run = [runs = m_refreshRuns, delayMs = refreshDelayMs, failure = refreshFailure] {
            ++*runs;
            QThread::msleep(delayMs);
            return failure;
        };
        job.apply = [this](const std::optional<ProviderFailure> &) {
            ++refreshCalls;
        };
        return job;
    }

    std::optional<ProviderFailure> prepare(const RefinementSettings &, const AccountSettings &) override
    {
        ++prepareCalls;
        if (onPrepare) {
            onPrepare();
        }
        return prepareFailure;
    }

    void refine(const QString &rawTranscript,
                const QStringList &vocabulary,
                const RefinementContext &context,
                const RefinementSettings &settings,
                const AccountSettings &) override
    {
        ++refineCalls;
        lastRawTranscript = rawTranscript;
        lastVocabulary = vocabulary;
        lastContext = context;
        lastBindingVocabulary = context.bindingVocabulary;
        lastStyle = settings.style;
        lastTone = context.tone;
        lastAnthropicModel = settings.anthropicModel;
        if (autoComplete) {
            emit completed(autoCompleteText);
        }
    }

    void cancel() override
    {
        ++cancelCalls;
    }

    void emitDeltaText(const QString &text)
    {
        emit delta(text);
    }

    void emitCompletedText(const QString &text)
    {
        emit completed(text);
    }

    void emitFailure(const QString &message, ProviderFailureKind kind = ProviderFailureKind::Other)
    {
        emit failed({kind, message});
    }

    QString providerId = QStringLiteral("openai");
    // The sign-in renews in a refresh job, on the worker.
    bool refreshRequired = false;
    unsigned long refreshDelayMs = 0;
    bool autoComplete = false;
    QString autoCompleteText;
    std::optional<ProviderFailure> refreshFailure;
    std::optional<ProviderFailure> prepareFailure;
    // Runs inside prepare(), as the event loop a keyring wait there spins.
    std::function<void()> onPrepare;
    // Refresh jobs run on the worker, and applied back on this thread.
    int refreshRuns() const
    {
        return *m_refreshRuns;
    }
    int refreshCalls = 0;
    int prepareCalls = 0;
    int refineCalls = 0;
    int cancelCalls = 0;
    QString lastRawTranscript;
    QStringList lastVocabulary;
    QStringList lastBindingVocabulary;
    RefinementContext lastContext;
    QString lastStyle;
    QString lastTone;
    QString lastAnthropicModel;

private:
    std::shared_ptr<std::atomic_int> m_refreshRuns = std::make_shared<std::atomic_int>(0);
};

class FakeDelivery final : public TextDeliveryAdapter {
public:
    explicit FakeDelivery(QObject *parent = nullptr)
        : TextDeliveryAdapter(parent)
    {
    }

    DeliveryResult deliver(const OutputSettings &settings,
                           const DeliveryContent &content,
                           const Target &target) override
    {
        ++calls;
        lastSettings = settings;
        lastContent = content;
        lastTarget = target;
        lastText = content.plainText;
        return result;
    }

    DeliveryResult result{true, DeliveryReceipt::InputSent, false, QStringLiteral("Sent to the app")};
    int calls = 0;
    OutputSettings lastSettings;
    DeliveryContent lastContent;
    Target lastTarget;
    QString lastText;
};

inline void registerFakeSpeechProvider(ProviderRegistry &registry,
                                       FakeSpeechTranscriber **speech,
                                       const QString &id = QStringLiteral("claude"),
                                       const QString &label = QStringLiteral("Fake Speech"))
{
    registry.registerSpeechProvider({id, label}, [speech, id](QObject *parent) {
        *speech = new FakeSpeechTranscriber(parent);
        (*speech)->providerId = id;
        return *speech;
    });
}

inline void registerFakeRefiner(ProviderRegistry &registry,
                                FakeRefiner **refiner,
                                const QString &id = QStringLiteral("openai"),
                                const QString &label = QStringLiteral("Fake Refiner"),
                                bool readsScreenshots = true)
{
    registry.registerRefinementProvider({id, label, readsScreenshots}, [refiner, id](QObject *parent) {
        *refiner = new FakeRefiner(parent);
        (*refiner)->providerId = id;
        return *refiner;
    });
}

} // namespace speecher::test
