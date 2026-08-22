#include "common/test_doubles.h"
#include "common/test_http.h"
#include "common/test_auth.h"
#include "dictation/StartupPreparationRunner.h"

#include <QSemaphore>
#include <QElapsedTimer>
#include <QTimer>

using namespace speecher::test;

class FakePopupPositioner final : public PopupPositioner {
public:
    explicit FakePopupPositioner(QObject *parent = nullptr)
        : PopupPositioner(parent)
    {
    }

    void configurePopup(QWidget *) override
    {
    }

    void positionBottomCenter(QWidget *) override
    {
    }
};


class DictationSessionLifecycleTests : public QObject {
    Q_OBJECT

private slots:
    void startupPreparationRunnerAppliesJobsInOrder()
    {
        StartupPreparationRunner runner;
        QStringList events;
        SpeechPrepareJob speechJob;
        speechJob.run = [&events] {
            events.append(QStringLiteral("speech run"));
            return SpeechPrepareResult{true, {}};
        };
        speechJob.apply = [&events](const SpeechPrepareResult &) {
            events.append(QStringLiteral("speech apply"));
        };
        RefinementRefreshJob refinerJob;
        refinerJob.run = [&events] {
            events.append(QStringLiteral("refiner run"));
            return RefinementRefreshResult{true, {}};
        };
        refinerJob.apply = [&events](const RefinementRefreshResult &) {
            events.append(QStringLiteral("refiner apply"));
        };
        QSignalSpy completed(&runner, &StartupPreparationRunner::completed);

        runner.start(17, std::move(speechJob), std::move(refinerJob), {true, {}});

        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 1000);
        QCOMPARE(events, QStringList({QStringLiteral("speech run"),
                                      QStringLiteral("refiner run"),
                                      QStringLiteral("speech apply"),
                                      QStringLiteral("refiner apply")}));
        const StartupPreparationResult result =
            qvariant_cast<StartupPreparationResult>(completed.first().first());
        QCOMPARE(result.generation, quint64(17));
        QVERIFY(result.speech.ok);
        QVERIFY(result.refinerRefreshAttempted);
        QVERIFY(result.refinerRefresh.ok);
    }

    void startupPreparationRunnerSkipsRefinerAfterSpeechFailure()
    {
        StartupPreparationRunner runner;
        bool speechApplied = false;
        bool refinerRan = false;
        SpeechPrepareJob speechJob;
        speechJob.run = [] {
            return SpeechPrepareResult{false, QStringLiteral("speech failed")};
        };
        speechJob.apply = [&speechApplied](const SpeechPrepareResult &) {
            speechApplied = true;
        };
        RefinementRefreshJob refinerJob;
        refinerJob.run = [&refinerRan] {
            refinerRan = true;
            return RefinementRefreshResult{true, {}};
        };
        QSignalSpy completed(&runner, &StartupPreparationRunner::completed);

        runner.start(18, std::move(speechJob), std::move(refinerJob), {true, {}});

        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 1000);
        const StartupPreparationResult result =
            qvariant_cast<StartupPreparationResult>(completed.first().first());
        QVERIFY(speechApplied);
        QVERIFY(!refinerRan);
        QVERIFY(!result.speech.ok);
        QCOMPARE(result.speech.message, QStringLiteral("speech failed"));
        QVERIFY(!result.refinerRefreshAttempted);
    }

    void startupPreparationRunnerReportsRefinerFailure()
    {
        StartupPreparationRunner runner;
        bool refinerApplied = false;
        RefinementRefreshJob refinerJob;
        refinerJob.run = [] {
            return RefinementRefreshResult{false, QStringLiteral("refresh failed")};
        };
        refinerJob.apply = [&refinerApplied](const RefinementRefreshResult &result) {
            refinerApplied = !result.ok;
        };
        QSignalSpy completed(&runner, &StartupPreparationRunner::completed);

        runner.start(19, std::nullopt, std::move(refinerJob), {true, {}});

        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 1000);
        const StartupPreparationResult result =
            qvariant_cast<StartupPreparationResult>(completed.first().first());
        QVERIFY(refinerApplied);
        QVERIFY(result.speech.ok);
        QVERIFY(result.refinerRefreshAttempted);
        QVERIFY(!result.refinerRefresh.ok);
        QCOMPARE(result.refinerRefresh.message, QStringLiteral("refresh failed"));
    }

    void startupPreparationRunnerCancellationSuppressesCompletion()
    {
        StartupPreparationRunner runner;
        QSemaphore started;
        QSemaphore release;
        bool applied = false;
        SpeechPrepareJob speechJob;
        speechJob.run = [&started, &release] {
            started.release();
            release.acquire();
            return SpeechPrepareResult{true, {}};
        };
        speechJob.apply = [&applied](const SpeechPrepareResult &) {
            applied = true;
        };
        QSignalSpy completed(&runner, &StartupPreparationRunner::completed);

        runner.start(20, std::move(speechJob), std::nullopt, {true, {}});
        QVERIFY(started.tryAcquire(1, 1000));
        runner.cancel();
        release.release();

        QTest::qWait(50);
        QCOMPARE(completed.count(), 0);
        QVERIFY(!applied);
    }

    void startupPreparationRunnerDestructionDoesNotWaitForBlockedJob()
    {
        const auto started = std::make_shared<QSemaphore>();
        const auto release = std::make_shared<QSemaphore>();
        auto runner = std::make_unique<StartupPreparationRunner>();
        SpeechPrepareJob speechJob;
        speechJob.run = [started, release] {
            started->release();
            release->acquire();
            return SpeechPrepareResult{true, {}};
        };
        runner->start(23, std::move(speechJob), std::nullopt, {true, {}});
        QVERIFY(started->tryAcquire(1, 1000));

        QElapsedTimer elapsed;
        elapsed.start();
        runner.reset();

        QVERIFY(elapsed.elapsed() < 500);
        release->release();
    }

    void startupPreparationRunnerIgnoresReplacedGeneration()
    {
        StartupPreparationRunner runner;
        QSemaphore started;
        QSemaphore release;
        bool staleApplied = false;
        SpeechPrepareJob staleJob;
        staleJob.run = [&started, &release] {
            started.release();
            release.acquire();
            return SpeechPrepareResult{true, {}};
        };
        staleJob.apply = [&staleApplied](const SpeechPrepareResult &) {
            staleApplied = true;
        };
        QSignalSpy completed(&runner, &StartupPreparationRunner::completed);

        runner.start(21, std::move(staleJob), std::nullopt, {true, {}});
        QVERIFY(started.tryAcquire(1, 1000));
        runner.start(22, std::nullopt, std::nullopt, {true, {}});
        release.release();

        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 1000);
        const StartupPreparationResult result =
            qvariant_cast<StartupPreparationResult>(completed.first().first());
        QCOMPARE(result.generation, quint64(22));
        QVERIFY(!staleApplied);
        QTest::qWait(50);
        QCOMPARE(completed.count(), 1);
    }

    void dictationSessionDeliversRawTranscript()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setPreviewWords(7);
        settings.setPauseMediaDuringTranscription(true);
        settings.setRefinementProvider(QStringLiteral("none"));
        settings.setCustomVocabulary({QStringLiteral("Speecher")});

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);
        QSignalSpy previewDisplay(&session, &DictationSession::previewDisplayChanged);

        session.startListening();
        QCOMPARE(int(session.state()), int(DictationState::Starting));
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        QVERIFY(audio->started);
        QCOMPARE(media->pauseCalls, 1);
        QCOMPARE(speech->prepareCalls, 1);
        QCOMPARE(speech->startCalls, 1);
        QCOMPARE(speech->lastVocabulary, QStringList{QStringLiteral("Speecher")});

        audio->pushAudio(QByteArrayLiteral("pcm"));
        QCOMPARE(speech->audioChunks.size(), 1);
        speech->emitFinalText(QStringLiteral("one two three four five six seven eight nine"));
        QCOMPARE(previewDisplay.last().first().toString(),
                 QStringLiteral("three four five six seven eight nine"));
        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);
        QCOMPARE(delivery->lastText, QStringLiteral("one two three four five six seven eight nine"));
        QCOMPARE(delivery->lastSettings.method, QString::fromLatin1(OutputMethod::Automatic));
        QCOMPARE(delivery->lastSettings.restoreClipboardAfterTyping, false);
        QCOMPARE(media->resumeCalls, 1);
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Idle), 1800);
    }

    void dictationSessionForwardsAudioDuringStartAndStop()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("none"));

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);
        audio->onStart = [&] { audio->pushAudio(QByteArrayLiteral("pre-roll")); };
        audio->onStop = [&] { audio->pushAudio(QByteArrayLiteral("post-roll")); };

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("captured"));
        session.stopListening();

        QCOMPARE(speech->audioChunks,
                 QList<QByteArray>({QByteArrayLiteral("pre-roll"), QByteArrayLiteral("post-roll")}));
    }

    void dictationSessionDoesNotResumeAfterCancellationInsideAudioStart()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("none"));

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);
        audio->onStart = [&] { session.stopListening(); };

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Idle), 250);

        QVERIFY(!audio->isActive());
        QCOMPARE(speech->cancelledAttempts, QList<quint64>({speech->currentAttemptId}));
    }

    void dictationCompletionStatus()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("none"));
        settings.setCompletionStatusDurationMs(900);

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        delivery->result = {
            true,
            DeliveryReceipt::VerifiedInTarget,
            false,
            QStringLiteral("Input sent"),
        };
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("finished dictation"));

        QSignalSpy message(&session, &DictationSession::popupMessageRequested);
        QSignalSpy status(&session, &DictationSession::statusChanged);
        QSignalSpy hidden(&session, &DictationSession::popupHideRequested);
        session.stopListening();

        QCOMPARE(message.count(), 1);
        QCOMPARE(message.first().first().toString(), QStringLiteral("Input sent"));
        QCOMPARE(status.last().first().toString(), QStringLiteral("Input sent"));
        QCOMPARE(int(session.state()), int(DictationState::Delivering));

        const QList<QTimer *> completionTimers =
            session.findChildren<QTimer *>(QString(), Qt::FindDirectChildrenOnly);
        QCOMPARE(completionTimers.size(), 1);
        QTimer *completionTimer = completionTimers.first();
        QVERIFY(completionTimer->isSingleShot());
        QCOMPARE(completionTimer->interval(), 900);
        QCOMPARE(completionTimer->timerType(), Qt::PreciseTimer);
        QVERIFY(completionTimer->isActive());
        completionTimer->stop();
        QVERIFY(QMetaObject::invokeMethod(completionTimer, "timeout", Qt::DirectConnection));
        QCOMPARE(hidden.count(), 1);
        QCOMPARE(int(session.state()), int(DictationState::Idle));
    }

    void dictationSessionToggleAndPushToTalkCommandsAreIdempotent()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("none"));
        settings.setOutputFormat(OutputFormat::PlainText);

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);

        session.toggleWithFormat(OutputFormat::Html);
        QCOMPARE(int(session.state()), int(DictationState::Starting));
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        QCOMPARE(speech->startCalls, 1);

        session.startListening();
        QCOMPARE(int(session.state()), int(DictationState::Listening));
        QCOMPARE(speech->startCalls, 1);

        speech->emitFinalText(QStringLiteral("toggle result"));
        session.toggle();
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 250);
        QCOMPARE(delivery->lastSettings.format, OutputFormat::Html);
        QCOMPARE(settings.outputFormat(), OutputFormat::PlainText);

        session.stopListening();
        QCOMPARE(delivery->calls, 1);
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Idle), 1800);

        session.stopListening();
        QCOMPARE(int(session.state()), int(DictationState::Idle));
    }

    void dictationSessionCancelsDeferredStartupOnFastRestart()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setPauseMediaDuringTranscription(true);
        settings.setRefinementProvider(QStringLiteral("none"));

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto target = std::make_unique<FakeTargetProvider>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings,
                                 audio.get(),
                                 media.get(),
                                 target.get(),
                                 delivery.get(),
                                 &registry);

        session.startListening();
        QCOMPARE(int(session.state()), int(DictationState::Starting));
        QCOMPARE(target->captureCalls, 0);
        session.stopListening();
        QCOMPARE(int(session.state()), int(DictationState::Idle));

        session.startListening();
        QCOMPARE(int(session.state()), int(DictationState::Starting));
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        QCOMPARE(target->captureCalls, 1);
        QCOMPARE(media->pauseCalls, 1);
        QCOMPARE(speech->startCalls, 1);
    }

    void dictationSessionWaitsForProviderCompletion()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("none"));

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->autoCompleteOnFinish = false;
        speech->emitFinalText(QStringLiteral("hello"));
        session.stopListening();

        QCOMPARE(int(session.state()), int(DictationState::Stopping));
        QCOMPARE(delivery->calls, 0);

        speech->emitFinalText(QStringLiteral("world"));
        QCOMPARE(delivery->calls, 0);
        speech->emitCompletion();

        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 250);
        QCOMPARE(delivery->lastText, QStringLiteral("hello world"));
    }

    void dictationSessionUsesPerSessionOutputFormatWithoutChangingDefault()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("none"));
        settings.setOutputFormat(OutputFormat::PlainText);

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);

        session.startListeningWithFormat(OutputFormat::Html);
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("<hello>"));
        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 250);
        QCOMPARE(delivery->lastSettings.format, OutputFormat::Html);
        QCOMPARE(delivery->lastContent.plainText, QStringLiteral("<hello>"));
        QVERIFY(delivery->lastContent.html);
        QCOMPARE(*delivery->lastContent.html, QStringLiteral("<p>&lt;hello&gt;</p>"));
        QCOMPARE(settings.outputFormat(), OutputFormat::PlainText);
    }

    void dictationSessionCapturesTargetAtStart()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("none"));
        settings.setAppRecognitionRules({
            {QStringLiteral("org.example.shell"), AppCategory::Terminal, std::nullopt},
        });

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto targetProvider = std::make_unique<FakeTargetProvider>();
        targetProvider->target.applicationId = QStringLiteral("org.kde.kate");
        targetProvider->target.category = AppCategory::CodeEditor;
        targetProvider->target.caretOffset = 42;
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(
            &settings,
            audio.get(),
            media.get(),
            targetProvider.get(),
            delivery.get(),
            &registry);

        session.startListening();
        QCOMPARE(int(session.state()), int(DictationState::Starting));
        QCOMPARE(targetProvider->captureCalls, 0);
        QTRY_COMPARE_WITH_TIMEOUT(targetProvider->captureCalls, 1, 250);
        QCOMPARE(targetProvider->lastRecognitionRules, settings.appRecognitionRules());
        QCOMPARE(int(session.state()), int(DictationState::Listening));
        speech->emitFinalText(QStringLiteral("hello"));
        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 250);
        QCOMPARE(delivery->lastTarget.applicationId, QStringLiteral("org.kde.kate"));
        QCOMPARE(delivery->lastTarget.caretOffset, 42);
    }

    void dictationSessionDefersTargetCaptureUntilPopupCanPaint()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("none"));

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto targetProvider = std::make_unique<FakeTargetProvider>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(
            &settings,
            audio.get(),
            media.get(),
            targetProvider.get(),
            delivery.get(),
            &registry);
        QSignalSpy shown(&session, &DictationSession::popupShowRequested);

        session.startListening();
        QCOMPARE(int(session.state()), int(DictationState::Starting));
        QCOMPARE(shown.count(), 1);
        QTest::qWait(20);
        QCOMPARE(targetProvider->captureCalls, 0);
        const quint64 generation = shown.first().first().toULongLong();
        session.popupPresented(generation + 1);
        QTest::qWait(20);
        QCOMPARE(targetProvider->captureCalls, 0);
        session.popupPresented(generation);
        QTRY_COMPARE_WITH_TIMEOUT(targetProvider->captureCalls, 1, 250);
        QCOMPARE(int(session.state()), int(DictationState::Listening));
    }

    void dictationSessionCapturesOptionalScreenshotOnlyForRefinement()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto targetProvider = std::make_unique<FakeTargetProvider>();
        auto screenshots = std::make_unique<FakeScreenshotContextProvider>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        FakeRefiner *refiner = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        registerFakeRefiner(registry, &refiner);
        DictationSession session(
            &settings,
            audio.get(),
            media.get(),
            targetProvider.get(),
            delivery.get(),
            &registry);
        session.setScreenshotContextProvider(screenshots.get());

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        QCOMPARE(screenshots->captureCalls, 0);
        speech->emitFinalText(QStringLiteral("first"));
        session.stopListening();
        QTRY_COMPARE_WITH_TIMEOUT(refiner->refineCalls, 1, 250);
        QVERIFY(!refiner->lastContext.hasScreenshot());
        refiner->emitCompletedText(QStringLiteral("first"));
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 250);
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Idle), 1800);

        settings.setIncludeScreenshotContext(true);
        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(screenshots->captureCalls, 1, 250);
        QCOMPARE(int(session.state()), int(DictationState::Listening));
        speech->emitFinalText(QStringLiteral("second"));
        session.stopListening();
        QTRY_COMPARE_WITH_TIMEOUT(refiner->refineCalls, 2, 250);
        QCOMPARE(refiner->lastContext.screenshotData, screenshots->data);
        QCOMPARE(refiner->lastContext.screenshotMediaType, screenshots->mediaType);
        refiner->emitCompletedText(QStringLiteral("second"));
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 2, 250);
        QVERIFY(screenshots->cancelCalls >= 2);
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Idle), 1800);

        refiner->screenshotCapable = false;
        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        QCOMPARE(screenshots->captureCalls, 1);
        session.stopListening();
    }

    void dictationSessionNeverCapturesScreenshotForSecureTarget()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));
        settings.setIncludeScreenshotContext(true);

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto targetProvider = std::make_unique<FakeTargetProvider>();
        targetProvider->target.applicationId = QStringLiteral("secure-fixture");
        targetProvider->target.accessible = true;
        targetProvider->target.secure = true;
        auto screenshots = std::make_unique<FakeScreenshotContextProvider>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        FakeRefiner *refiner = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        registerFakeRefiner(registry, &refiner);
        DictationSession session(
            &settings,
            audio.get(),
            media.get(),
            targetProvider.get(),
            delivery.get(),
            &registry);
        session.setScreenshotContextProvider(screenshots.get());

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        QCOMPARE(screenshots->captureCalls, 0);
        session.stopListening();
    }

    void livePortalScreenshotCapture()
    {
        if (qEnvironmentVariableIsEmpty("SPEECHER_LIVE_SCREENSHOT_TEST")) {
            QSKIP("Set SPEECHER_LIVE_SCREENSHOT_TEST=1 inside a desktop session");
        }

        PortalScreenshotContextProvider screenshots;
        QSignalSpy captured(&screenshots, &PortalScreenshotContextProvider::captured);
        QSignalSpy failed(&screenshots, &PortalScreenshotContextProvider::failed);
        screenshots.capture();

        QTRY_VERIFY_WITH_TIMEOUT(!captured.isEmpty() || !failed.isEmpty(), 15000);
        const QString failureMessage = failed.isEmpty()
            ? QString()
            : failed.first().first().toString();
        QVERIFY2(failed.isEmpty(), qPrintable(failureMessage));
        QVERIFY(captured.first().at(0).toByteArray().size() > 100);
        QCOMPARE(captured.first().at(1).toString(), QStringLiteral("image/png"));
    }

    void dictationSessionDoesNotReplayAudioAfterProviderFailure()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("none"));

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->autoCompleteOnFinish = false;
        audio->pushAudio(QByteArrayLiteral("pcm"));
        session.stopListening();
        speech->emitFailure(QStringLiteral("temporary disconnect"), true);

        QCOMPARE(speech->startCalls, 1);
        QCOMPARE(speech->audioChunks, QList<QByteArray>({QByteArrayLiteral("pcm")}));
        QCOMPARE(delivery->calls, 0);
        QCOMPARE(int(session.state()), int(DictationState::Error));
    }

    void dictationSessionBackgroundSpeechPreparationDoesNotBlockStartup()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("none"));

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        registry.speechProvider(QStringLiteral("claude"));
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);
        speech->backgroundPrepare = true;
        speech->backgroundPrepareDelayMs = 180;
        speech->refreshRequired = true;

        QSignalSpy refreshSpy(&session, &DictationSession::popupOAuthRefreshRequested);
        QElapsedTimer timer;
        timer.start();
        session.startListening();

        QVERIFY(timer.elapsed() < 100);
        QCOMPARE(int(session.state()), int(DictationState::Starting));
        QCOMPARE(refreshSpy.count(), 0);
        QVERIFY(!audio->started);

        QTRY_COMPARE_WITH_TIMEOUT(refreshSpy.count(), 1, 250);
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 1000);
        QCOMPARE(speech->backgroundPrepareCalls, 1);
        QCOMPARE(speech->prepareCalls, 1);
        QCOMPARE(speech->startCalls, 1);
        QVERIFY(audio->started);
    }

    void dictationStartupFailureKeepsPopupOpenWithMessage()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("none"));

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        registry.speechProvider(QStringLiteral("claude"));
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);
        speech->prepareResult = {
            false,
            QStringLiteral("Claude login cannot be refreshed"),
        };

        QSignalSpy shown(&session, &DictationSession::popupShowRequested);
        QSignalSpy hidden(&session, &DictationSession::popupHideRequested);
        const int errorSignalIndex = session.metaObject()->indexOfSignal(
            "popupErrorRequested(QString)");
        QVERIFY(errorSignalIndex >= 0);
        QSignalSpy message(
            &session,
            session.metaObject()->method(errorSignalIndex));

        session.startListening();

        QCOMPARE(int(session.state()), int(DictationState::Starting));
        QCOMPARE(shown.count(), 1);
        QCOMPARE(message.count(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Error), 250);
        QCOMPARE(message.count(), 1);
        QCOMPARE(message.first().first().toString(),
                 QStringLiteral("Claude login cannot be refreshed"));
        QTest::qWait(1900);
        QCOMPARE(int(session.state()), int(DictationState::Error));
        QCOMPARE(hidden.count(), 0);

        session.stopListening();
        QCOMPARE(int(session.state()), int(DictationState::Idle));
        QCOMPARE(hidden.count(), 1);
    }

    void dictationSessionBackgroundRefinerRefreshDoesNotBlockStartup()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        FakeRefiner *refiner = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        registerFakeRefiner(registry, &refiner);
        registry.speechProvider(QStringLiteral("claude"));
        registry.refinementProvider(QStringLiteral("openai"));
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);
        refiner->backgroundRefresh = true;
        refiner->backgroundRefreshDelayMs = 180;
        refiner->refreshRequired = true;

        QSignalSpy refreshSpy(&session, &DictationSession::popupOAuthRefreshRequested);
        QElapsedTimer timer;
        timer.start();
        session.startListening();

        QVERIFY(timer.elapsed() < 100);
        QCOMPARE(int(session.state()), int(DictationState::Starting));
        QCOMPARE(refreshSpy.count(), 0);
        QVERIFY(!audio->started);

        QTRY_COMPARE_WITH_TIMEOUT(refreshSpy.count(), 1, 250);
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 1000);
        QCOMPARE(refiner->backgroundRefreshCalls, 1);
        QCOMPARE(refiner->refreshCalls, 1);
        QCOMPARE(speech->startCalls, 1);
        QVERIFY(audio->started);
    }

    void transcriberPopupRestoresPreviewLayoutAfterOAuthIndicator()
    {
        TranscriberPopup popup(new FakePopupPositioner);
        auto *layout = qobject_cast<QBoxLayout *>(popup.layout());
        auto *previewPill = popup.findChild<QFrame *>(QStringLiteral("previewPill"));
        auto *rawTranscript = popup.findChild<QLabel *>(QStringLiteral("rawTranscript"));
        auto *waveform = popup.findChild<WaveformWidget *>();
        QVERIFY(layout);
        QVERIFY(previewPill);
        QVERIFY(rawTranscript);
        QVERIFY(waveform);
        QVERIFY(!popup.findChild<QLabel *>(QStringLiteral("popupStatus")));
        QVERIFY(!popup.findChild<QLabel *>(QStringLiteral("popupMetadata")));
        QCOMPARE(previewPill->minimumHeight(), 48);
        QCOMPARE(previewPill->maximumHeight(), 48);
        QVERIFY(!rawTranscript->wordWrap());

        popup.showOAuthRefreshIndicator();
        QVERIFY(layout->indexOf(previewPill) < layout->indexOf(waveform));

        popup.setPreview(QStringLiteral("hello world"));
        QVERIFY(layout->indexOf(waveform) < layout->indexOf(previewPill));

        popup.showOAuthRefreshIndicator();
        popup.hidePreview();
        QVERIFY(layout->indexOf(waveform) < layout->indexOf(previewPill));

        const QString longRaw = QStringLiteral(
            "one two three four five six seven eight nine ten eleven twelve");
        popup.setPreview(longRaw);
        QVERIFY(!rawTranscript->text().contains(QLatin1Char('\n')));
        popup.setRefining(true);
        QVERIFY(!rawTranscript->isHidden());
    }

    void transcriberPopupShowsLongErrorsInOneReadablePill()
    {
        TranscriberPopup popup(new FakePopupPositioner);
        auto *previewPill = popup.findChild<QFrame *>(QStringLiteral("previewPill"));
        auto *rawTranscript = popup.findChild<QLabel *>(QStringLiteral("rawTranscript"));
        auto *waveform = popup.findChild<WaveformWidget *>();
        auto *dismissProgress = popup.findChild<QProgressBar *>(
            QStringLiteral("errorDismissProgress"));
        QVERIFY(previewPill);
        QVERIFY(rawTranscript);
        QVERIFY(waveform);
        QVERIFY(dismissProgress);
        const QString error = QStringLiteral(
            "Claude login cannot be refreshed; run `claude` in a terminal and use the `/login` command");

        popup.show();
        QVERIFY(QMetaObject::invokeMethod(
            &popup,
            "showErrorMessage",
            Q_ARG(QString, error)));

        QVERIFY(waveform->isHidden());
        QVERIFY(!previewPill->isHidden());
        QVERIFY(rawTranscript->wordWrap());
        QCOMPARE(rawTranscript->text(), error);
        QVERIFY(previewPill->width() > waveform->width());
        QVERIFY(dismissProgress->isVisible());
        QCOMPARE(dismissProgress->value(), dismissProgress->maximum());
        QTest::qWait(150);
        QVERIFY(dismissProgress->value() < dismissProgress->maximum());
        QVERIFY(dismissProgress->value() > dismissProgress->minimum());
        QTRY_VERIFY_WITH_TIMEOUT(popup.isHidden(), 5500);
    }
};

int runDictationSessionLifecycleTests(int argc, char **argv)
{
    DictationSessionLifecycleTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_dictation_session_lifecycle.moc"
