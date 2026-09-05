#pragma once

#include "test_prelude.h"

#include <functional>

namespace speecher::test {

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
    bool started = false;
    bool active = false;
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

    Target capture(const QList<AppRecognitionRule> &recognitionRules = {}) override
    {
        ++captureCalls;
        lastRecognitionRules = recognitionRules;
        return target;
    }

    bool stillFocused(const Target &) override
    {
        return focused;
    }

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
    QList<AppRecognitionRule> lastRecognitionRules;
    int captureCalls = 0;
    int insertCalls = 0;
    bool focused = true;
    bool verified = false;
    bool directInsertionAvailable = false;
    bool inserted = false;
    QString insertedText;
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

    QString id() const override
    {
        return QStringLiteral("claude");
    }

    QString label() const override
    {
        return QStringLiteral("Fake Speech");
    }

    bool requiresRefresh(const SpeechSettings &) const override
    {
        return refreshRequired;
    }

    std::optional<SpeechPrepareJob> createPrepareJob(const SpeechSettings &) override
    {
        if (!backgroundPrepare) {
            return std::nullopt;
        }

        SpeechPrepareJob job;
        job.showRefreshIndicator = refreshRequired;
        job.run = [this] {
            ++backgroundPrepareCalls;
            if (backgroundPrepareDelayMs > 0) {
                QThread::msleep(backgroundPrepareDelayMs);
            }
            return prepareResult;
        };
        job.apply = [this](const SpeechPrepareResult &) {
            ++prepareCalls;
        };
        return job;
    }

    SpeechPrepareResult prepare(const SpeechSettings &) override
    {
        ++prepareCalls;
        return prepareResult;
    }

    void startAttempt(quint64 attemptId, const SpeechSettings &settings) override
    {
        ++startCalls;
        currentAttemptId = attemptId;
        lastVocabulary = settings.vocabulary;
    }

    void sendAudio(quint64 attemptId, const QByteArray &pcm) override
    {
        if (attemptId == currentAttemptId) {
            audioChunks << pcm;
        }
    }

    void finishInput(quint64 attemptId) override
    {
        ++stopCalls;
        if (autoCompleteOnFinish) {
            emit attemptCompleted(attemptId);
        }
    }

    void cancelAttempt(quint64 attemptId) override
    {
        cancelledAttempts.append(attemptId);
    }

    void emitPartialText(const QString &text)
    {
        emit partialTranscript(currentAttemptId, text);
    }

    void emitFinalText(const QString &text)
    {
        emit finalTranscript(currentAttemptId, text);
    }

    void emitFailure(const QString &message, bool retryable = false, const QString &phase = {})
    {
        emit failed({currentAttemptId, message, retryable, phase});
    }

    void emitCompletion()
    {
        emit attemptCompleted(currentAttemptId);
    }

    bool refreshRequired = false;
    bool backgroundPrepare = false;
    unsigned long backgroundPrepareDelayMs = 0;
    SpeechPrepareResult prepareResult{true, {}};
    int backgroundPrepareCalls = 0;
    int prepareCalls = 0;
    int startCalls = 0;
    int stopCalls = 0;
    quint64 currentAttemptId = 0;
    bool autoCompleteOnFinish = true;
    QList<quint64> cancelledAttempts;
    QList<QByteArray> audioChunks;
    QStringList lastVocabulary;
};

class FakeRefiner final : public TranscriptRefiner {
public:
    explicit FakeRefiner(QObject *parent = nullptr)
        : TranscriptRefiner(parent)
    {
    }

    QString id() const override
    {
        return QStringLiteral("openai");
    }

    QString label() const override
    {
        return QStringLiteral("Fake Refiner");
    }

    bool requiresRefresh(const RefinementSettings &) const override
    {
        return refreshRequired;
    }

    std::optional<RefinementRefreshJob> createRefreshJob(const RefinementSettings &) override
    {
        if (!backgroundRefresh || !refreshRequired) {
            return std::nullopt;
        }

        RefinementRefreshJob job;
        job.showRefreshIndicator = true;
        job.run = [this] {
            ++backgroundRefreshCalls;
            if (backgroundRefreshDelayMs > 0) {
                QThread::msleep(backgroundRefreshDelayMs);
            }
            return refreshResult;
        };
        job.apply = [this](const RefinementRefreshResult &) {
            ++refreshCalls;
        };
        return job;
    }

    void refresh(const RefinementSettings &) override
    {
        ++refreshCalls;
    }

    RefinementPrepareResult prepare(const RefinementSettings &) override
    {
        ++prepareCalls;
        return prepareResult;
    }

    bool supportsScreenshotContext(const RefinementSettings &) const override
    {
        return screenshotCapable;
    }

    void refine(const QString &rawTranscript,
                const QStringList &vocabulary,
                const RefinementContext &context,
                const RefinementSettings &settings) override
    {
        ++refineCalls;
        lastRawTranscript = rawTranscript;
        lastVocabulary = vocabulary;
        lastContext = context;
        lastBindingVocabulary = settings.bindingVocabulary;
        lastStyle = settings.style;
        lastTone = settings.tone;
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

    void emitFailure(const QString &message)
    {
        emit failed(message);
    }

    bool refreshRequired = false;
    bool backgroundRefresh = false;
    unsigned long backgroundRefreshDelayMs = 0;
    bool autoComplete = false;
    bool screenshotCapable = true;
    QString autoCompleteText;
    RefinementRefreshResult refreshResult{true, {}};
    RefinementPrepareResult prepareResult{true, {}};
    int backgroundRefreshCalls = 0;
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

    DeliveryResult result{true, DeliveryReceipt::InputSent, false, QStringLiteral("Input sent")};
    int calls = 0;
    OutputSettings lastSettings;
    DeliveryContent lastContent;
    Target lastTarget;
    QString lastText;
};

inline void registerFakeSpeechProvider(ProviderRegistry &registry, FakeSpeechTranscriber **speech)
{
    registry.registerSpeechProvider({QStringLiteral("claude"), QStringLiteral("Fake Speech")}, [speech](QObject *parent) {
        *speech = new FakeSpeechTranscriber(parent);
        return *speech;
    });
}

inline void registerFakeRefiner(ProviderRegistry &registry, FakeRefiner **refiner)
{
    registry.registerRefinementProvider({QStringLiteral("openai"), QStringLiteral("Fake Refiner")}, [refiner](QObject *parent) {
        *refiner = new FakeRefiner(parent);
        return *refiner;
    });
}

} // namespace speecher::test
