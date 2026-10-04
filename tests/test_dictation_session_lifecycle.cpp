#include "common/test_prelude.h"
#include "common/test_doubles.h"
#include "dictation/StartupPreparationRunner.h"

#include <QSemaphore>
#include <QElapsedTimer>
#include <QTimer>

#include <utility>

using namespace speecher::test;

class FakePopupPositioner final : public PopupPositioner {
public:
    explicit FakePopupPositioner(QObject *parent = nullptr)
        : PopupPositioner(parent)
    {
    }

    void positionBottomCenter(PopupSurface &) override
    {
    }
};


class DictationSessionLifecycleTests : public QObject {
    Q_OBJECT

private slots:
    // Every status line and Start/Stop control on the three platforms reads
    // these, so each state's words are pinned here once.
    void statusLabelAndToggleSayWhatEachStateMeans()
    {
        QCOMPARE(dictationStatusLabel(QStringLiteral("idle")), QStringLiteral("Idle"));
        QCOMPARE(dictationStatusLabel(QStringLiteral("starting")), QStringLiteral("Listening…"));
        QCOMPARE(dictationStatusLabel(QStringLiteral("listening")), QStringLiteral("Listening…"));
        QCOMPARE(dictationStatusLabel(QStringLiteral("stopping")), QStringLiteral("Transcribing…"));
        QCOMPARE(dictationStatusLabel(QStringLiteral("refining")), QStringLiteral("Refining…"));
        QCOMPARE(dictationStatusLabel(QStringLiteral("delivering")), QStringLiteral("Delivering…"));
        QCOMPARE(dictationStatusLabel(QStringLiteral("delivering"), QStringLiteral("Copied")),
                 QStringLiteral("Copied"));
        QCOMPARE(dictationStatusLabel(QStringLiteral("error"), QStringLiteral("Microphone unavailable")),
                 QStringLiteral("Microphone unavailable"));
        // A message outside delivering and error is the last session's.
        QCOMPARE(dictationStatusLabel(QStringLiteral("idle"), QStringLiteral("Copied")), QStringLiteral("Idle"));

        QCOMPARE(dictationToggleAction(QStringLiteral("idle")).label, QStringLiteral("Start dictation"));
        QCOMPARE(dictationToggleAction(QStringLiteral("listening")).label, QStringLiteral("Stop dictation"));
        QCOMPARE(dictationToggleAction(QStringLiteral("refining")).label, QStringLiteral("Cancel refinement"));
        QVERIFY(dictationToggleAction(QStringLiteral("refining")).enabled);
        QVERIFY(!dictationToggleAction(QStringLiteral("stopping")).enabled);
        QVERIFY(!dictationToggleAction(QStringLiteral("delivering")).enabled);
        QVERIFY(dictationToggleAction(QStringLiteral("error")).enabled);
    }

    void popupErrorsStayLongEnoughToRead()
    {
        QCOMPARE(popupErrorDismissMs(QStringLiteral("Microphone unavailable")), 8000);
        QCOMPARE(popupErrorDismissMs(QString(200, u'a')), 12000);
        QCOMPARE(popupErrorDismissMs(QString(2000, u'a')), 30000);
    }

    // The tray panels' transcript is the preview, then the delivered text, and
    // outlives the session: a new one starting keeps it until it hears words,
    // and a failed one leaves what it heard.
    void theControllerKeepsTheLastTranscriptAcrossSessions()
    {
        ApplicationController controller(true);
        QSignalSpy changed(&controller, &ApplicationController::lastTranscriptChanged);
        DictationSession *session = controller.session();

        emit session->previewChanged(QStringLiteral("draft words"));
        QCOMPARE(controller.lastTranscript(), QStringLiteral("draft words"));
        emit session->transcriptDelivered(QStringLiteral("Draft words."));
        QCOMPARE(controller.lastTranscript(), QStringLiteral("Draft words."));

        emit session->previewChanged(QString());
        QCOMPARE(controller.lastTranscript(), QStringLiteral("Draft words."));
        emit session->previewChanged(QStringLiteral("words of a failed attempt"));
        QCOMPARE(controller.lastTranscript(), QStringLiteral("words of a failed attempt"));
        QCOMPARE(changed.count(), 3);
    }

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

    // The speech request carries only the terms for the Writing Profile the
    // target settles.
    void speechKeyTermsFollowTheWritingProfile()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("none"));
        settings.setDefaultWritingProfile(WritingProfile::Work);
        VocabularyEntry kubernetes{QStringLiteral("Kubernetes")};
        kubernetes.profiles = {WritingProfile::Work};
        VocabularyEntry lucia{QStringLiteral("Lúcia")};
        lucia.profiles = {WritingProfile::Personal};
        settings.setVocabularyEntries({kubernetes, lucia});

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        QCOMPARE(speech->lastVocabulary, QStringList{QStringLiteral("Kubernetes")});
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

    void dictationSessionOutlivesItsProviderRegistry()
    {
        // The controller's registry is created before the session and destroyed
        // before it. After a dictation the session holds the registry's
        // transcriber; its destructor must not call into the freed object.
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));
        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        auto *registry = new ProviderRegistry;
        FakeSpeechTranscriber *speech = nullptr;
        FakeRefiner *refiner = nullptr;
        registerFakeSpeechProvider(*registry, &speech);
        registerFakeRefiner(*registry, &refiner);
        auto *session = new DictationSession(&settings, audio.get(), media.get(), delivery.get(), registry);
        session->startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session->state()), int(DictationState::Listening), 250);
        QVERIFY(speech);
        speech->emitFinalText(QStringLiteral("hello"));
        session->stopListening();
        QTRY_VERIFY_WITH_TIMEOUT(refiner && refiner->refineCalls == 1, 1000);
        refiner->emitCompletedText(QStringLiteral("hello"));
        QTRY_COMPARE_WITH_TIMEOUT(int(session->state()), int(DictationState::Idle), 1800);

        // Passing means surviving: the owner of *speech and *refiner goes away
        // first, as in the controller, and the session's destructor must not
        // touch either dead object.
        delete registry;
        delete session;
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
        audio->onStart = [&] {
            session.stopListening();
            audio->startResult = false;
        };

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
        QSignalSpy state(&session, &DictationSession::stateChanged);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("finished dictation"));

        QSignalSpy message(&session, &DictationSession::popupMessageRequested);
        QSignalSpy status(&session, &DictationSession::statusChanged);
        QSignalSpy hidden(&session, &DictationSession::popupHideRequested);
        session.stopListening();

        QCOMPARE(message.count(), 1);
        QCOMPARE(message.first().first().toString(), QStringLiteral("Input sent"));
        QCOMPARE(message.first().at(1).value<PopupOutcome>(), PopupOutcome::Inserted);
        QCOMPARE(status.last().first().toString(), QStringLiteral("Input sent"));
        QCOMPARE(state.last().first().toString(), QStringLiteral("delivering"));
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
        QCOMPARE(state.last().first().toString(), QStringLiteral("idle"));
        QCOMPARE(int(session.state()), int(DictationState::Idle));

        // A clipboard-only receipt reports itself as copied, not sent.
        delivery->result = {true, DeliveryReceipt::Copied, false, QStringLiteral("Copied")};
        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("copied dictation"));
        message.clear();
        session.stopListening();
        QCOMPARE(message.count(), 1);
        QCOMPARE(message.first().at(1).value<PopupOutcome>(), PopupOutcome::Copied);
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

        session.toggleWith({OutputFormat::Html, std::nullopt});
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

    void attemptTranscriptReplacesOnlyTextFromTheCurrentAttempt()
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
        speech->emitFinalText(QStringLiteral("sentence a"));
        // A mid-dictation stream drop reconnects on a fresh attempt; the
        // accuracy pass of that attempt covers only the audio that follows.
        speech->emitFailure(QStringLiteral("stream closed"), true, QStringLiteral("streaming"));
        QTRY_COMPARE_WITH_TIMEOUT(speech->startCalls, 2, 250);
        speech->emitFinalText(QStringLiteral("sentence bee"));

        speech->autoCompleteOnFinish = false;
        session.stopListening();
        speech->emitAttemptText(QStringLiteral("sentence b"));
        speech->emitCompletion();

        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 250);
        QCOMPARE(delivery->lastText, QStringLiteral("sentence a sentence b"));
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

        session.startListeningWith({OutputFormat::Html, std::nullopt});
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

    void deliveredDictationIsRecordedOnlyWhenInsightsAreOn_data()
    {
        QTest::addColumn<bool>("enabled");
        QTest::addColumn<bool>("delivered");
        QTest::newRow("on") << true << true;
        QTest::newRow("off") << false << true;
        QTest::newRow("delivery failed") << true << false;
    }

    void deliveredDictationIsRecordedOnlyWhenInsightsAreOn()
    {
        QFETCH(bool, enabled);
        QFETCH(bool, delivered);
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("none"));
        settings.setInsightsEnabled(enabled);

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto targetProvider = std::make_unique<FakeTargetProvider>();
        targetProvider->target.applicationName = QStringLiteral("Kate");
        targetProvider->target.processName = QStringLiteral("kate");
        auto delivery = std::make_unique<FakeDelivery>();
        if (!delivered) {
            delivery->result = {false, DeliveryReceipt::None, false, QStringLiteral("No target")};
        }
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(
            &settings, audio.get(), media.get(), targetProvider.get(), delivery.get(), &registry);
        QSignalSpy recorded(&session, &DictationSession::dictationRecorded);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("Hello, dear world."));
        QTest::qWait(20);
        session.stopListening();
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);

        QCOMPARE(recorded.count(), enabled && delivered ? 1 : 0);
        if (!recorded.isEmpty()) {
            const auto record = recorded.first().first().value<DictationRecord>();
            QCOMPARE(record.words, 3);
            QCOMPARE(record.appName, QStringLiteral("Kate"));
            QVERIFY(record.audioMs > 0);
        }
    }

    // The saved language is one Claude Voice lacks: the popup says so and
    // offers the Dictation page, and nothing reaches the service. --language
    // replaces it for one session.
    void aSpokenLanguageTheServiceLacksStopsTheDictation()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("none"));
        settings.setSpokenLanguage(QStringLiteral("cy"));
        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);
        QSignalSpy message(&session, &DictationSession::popupErrorRequested);

        session.startListening();
        QCOMPARE(int(session.state()), int(DictationState::Error));
        QCOMPARE(message.count(), 1);
        QCOMPARE(message.first().first().toString(),
                 QStringLiteral("Fake Speech can't listen for Welsh. Choose another Spoken Language."));
        QCOMPARE(message.first().at(1).value<PopupErrorAction>().pageId, QStringLiteral("dictation"));
        QCOMPARE(speech->startCalls, 0);

        session.stopListening();
        session.startListeningWith({std::nullopt, std::nullopt, QStringLiteral("de")});
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        QCOMPARE(speech->lastLanguage, QStringLiteral("de"));
        QCOMPARE(settings.spokenLanguage(), QStringLiteral("cy"));
        settings.raw().clear();
    }

    void aForcedWritingProfileHoldsForTheWholeSession()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("none"));
        AppSettings draft = settings.snapshot();
        draft.refinement.writingProfiles.append({QStringLiteral("custom_stand_up"), QStringLiteral("balanced"),
                                                 QStringLiteral("none"), QString(), QStringLiteral("Stand up")});
        settings.applySnapshot(draft);

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto targetProvider = std::make_unique<FakeTargetProvider>();
        targetProvider->target.applicationId = QStringLiteral("org.kde.kate");
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(
            &settings, audio.get(), media.get(), targetProvider.get(), delivery.get(), &registry);
        QSignalSpy recorded(&session, &DictationSession::dictationRecorded);

        session.startListeningWith({std::nullopt, QStringLiteral("custom_stand_up")});
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        // Deleting the profile mid-session leaves this session's copy alone.
        draft = settings.snapshot();
        draft.refinement.writingProfiles = defaultWritingProfileSettings();
        settings.applySnapshot(draft);
        speech->emitFinalText(QStringLiteral("Stand up notes"));
        session.stopListening();
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);

        QCOMPARE(recorded.count(), 1);
        const auto record = recorded.first().first().value<DictationRecord>();
        QCOMPARE(record.profile, QStringLiteral("custom_stand_up"));
        QCOMPARE(record.profileName, QStringLiteral("Stand up"));
    }

    void selectionEditRecordsTheSpokenInstruction()
    {
        // The refiner returns the whole revised selection; the record counts
        // what was dictated, not that.
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));
        settings.setInsightsEnabled(true);

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto targetProvider = std::make_unique<FakeTargetProvider>();
        targetProvider->target.selectedText = QStringLiteral("the release is tomorrow");
        targetProvider->target.selectionStart = 0;
        targetProvider->target.selectionEnd = 23;
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        FakeRefiner *refiner = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        registerFakeRefiner(registry, &refiner);
        DictationSession session(
            &settings, audio.get(), media.get(), targetProvider.get(), delivery.get(), &registry);
        QSignalSpy recorded(&session, &DictationSession::dictationRecorded);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("Make it louder"));
        refiner->autoComplete = true;
        refiner->autoCompleteText = QStringLiteral("THE RELEASE IS TOMORROW, EVERYONE, BE READY!");
        session.stopListening();
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);

        QCOMPARE(recorded.count(), 1);
        QCOMPARE(recorded.first().first().value<DictationRecord>().words, 3);
    }

    void turningInsightsOffMidSessionRecordsNothing()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("none"));
        settings.setInsightsEnabled(true);

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);
        QSignalSpy recorded(&session, &DictationSession::dictationRecorded);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("Hello"));
        settings.setInsightsEnabled(false);
        session.stopListening();
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);

        QCOMPARE(recorded.count(), 0);
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

        // The startup fallback proceeds without the popup after a delay; a
        // slow runner can pass that delay inside processEvents(), so hold it
        // off for this test and restore the default afterwards.
        DictationSession::setPopupPaintFallbackMs(60000);
        const auto restoreFallback = qScopeGuard([] { DictationSession::setPopupPaintFallbackMs(50); });

        session.startListening();
        QCOMPARE(int(session.state()), int(DictationState::Starting));
        QCOMPARE(shown.count(), 1);
        QCoreApplication::processEvents();
        QCOMPARE(targetProvider->captureCalls, 0);
        const quint64 generation = shown.first().first().toULongLong();
        session.popupPresented(generation + 1);
        QCoreApplication::processEvents();
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

    void cancelForShutdownDuringRefinementNeverDelivers()
    {
        // Quitting mid-refinement used to route through stopListening(),
        // whose Refining branch delivers the fallback transcript — a paste
        // into whatever window has focus, during shutdown.
        SettingsStore settings;
        settings.raw().clear();
        settings.setPauseMediaDuringTranscription(true);
        settings.setRefinementProvider(QStringLiteral("openai"));

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        FakeRefiner *refiner = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        registerFakeRefiner(registry, &refiner);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("quit must not paste this"));
        session.stopListening();
        QTRY_COMPARE_WITH_TIMEOUT(refiner->refineCalls, 1, 250);
        QCOMPARE(int(session.state()), int(DictationState::Refining));

        session.cancelForShutdown();

        QCOMPARE(int(session.state()), int(DictationState::Idle));
        QCOMPARE(delivery->calls, 0);
        QCOMPARE(refiner->cancelCalls, 1);
        QVERIFY(media->resumeCalls >= 1);
        // A refinement completion that lands after shutdown is stale and
        // must stay undelivered.
        refiner->emitCompletedText(QStringLiteral("quit must not paste this"));
        QTest::qWait(10);
        QCOMPARE(delivery->calls, 0);
        QCOMPARE(int(session.state()), int(DictationState::Idle));
    }

    // Cancel throws the session away from Starting through Refining: nothing
    // is delivered or recorded, paused media resumes, the popup says
    // "Canceled", and a speech final or refinement result that lands
    // afterwards is stale.
    void cancelDiscardsTheSessionInEveryActiveState()
    {
        for (const DictationState target : {DictationState::Starting, DictationState::Listening,
                                            DictationState::Paused, DictationState::Stopping,
                                            DictationState::Refining}) {
            SettingsStore settings;
            settings.raw().clear();
            settings.setPauseMediaDuringTranscription(true);
            settings.setInsightsEnabled(true);
            settings.setRefinementProvider(QStringLiteral("openai"));
            auto audio = std::make_unique<FakeAudioInput>();
            auto media = std::make_unique<FakeMediaController>();
            auto delivery = std::make_unique<FakeDelivery>();
            ProviderRegistry registry;
            FakeSpeechTranscriber *speech = nullptr;
            FakeRefiner *refiner = nullptr;
            registerFakeSpeechProvider(registry, &speech);
            registerFakeRefiner(registry, &refiner);
            DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);
            QSignalSpy recorded(&session, &DictationSession::dictationRecorded);
            QSignalSpy message(&session, &DictationSession::popupMessageRequested);
            QSignalSpy hidden(&session, &DictationSession::popupHideRequested);

            session.startListening();
            if (target != DictationState::Starting) {
                QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
                speech->emitFinalText(QStringLiteral("never pasted"));
            }
            if (target == DictationState::Stopping) {
                speech->autoCompleteOnFinish = false;
            }
            if (target == DictationState::Stopping || target == DictationState::Refining) {
                session.stopListening();
            }
            if (target == DictationState::Paused) {
                session.pause();
            }
            QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(target), 250);

            session.cancel();

            QCOMPARE(int(session.state()), int(DictationState::Idle));
            QCOMPARE(message.count(), 1);
            QCOMPARE(message.first().at(0).toString(), QStringLiteral("Canceled"));
            QCOMPARE(message.first().at(1).value<PopupOutcome>(), PopupOutcome::Cancelled);
            QVERIFY(media->resumeCalls >= 1);
            if (target == DictationState::Refining) {
                QCOMPARE(refiner->cancelCalls, 1);
            }
            speech->emitFinalText(QStringLiteral("late final"));
            speech->emitCompletion();
            refiner->emitCompletedText(QStringLiteral("late refinement"));
            QTest::qWait(10);
            QCOMPARE(int(session.state()), int(DictationState::Idle));
            QCOMPARE(delivery->calls, 0);
            QCOMPARE(recorded.count(), 0);
            if (target == DictationState::Listening) {
                QTRY_COMPARE_WITH_TIMEOUT(hidden.count(), 1, 2000);
            }
        }
    }

    // An error is dismissed without a "Canceled"; idle and delivering, when
    // the text is already out, are left alone.
    void cancelDismissesAnErrorAndLeavesIdleAndDeliveringAlone()
    {
        SettingsStore settings;
        settings.raw().clear();
        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);
        QSignalSpy message(&session, &DictationSession::popupMessageRequested);
        QSignalSpy states(&session, &DictationSession::stateChanged);

        session.cancel();
        QCOMPARE(states.count(), 0);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("delivered"));
        session.stopListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Delivering), 250);
        session.cancel();
        QCOMPARE(int(session.state()), int(DictationState::Delivering));
        QCOMPARE(delivery->calls, 1);
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Idle), 3000);

        speech->prepareResult = {false, QStringLiteral("Sign in again")};
        message.clear();
        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Error), 250);
        session.cancel();
        QCOMPARE(int(session.state()), int(DictationState::Idle));
        QCOMPARE(message.count(), 0);
    }

    // Pause finishes the attempt in flight and keeps its words, including a
    // final that lands after resume; the next attempt opens only once the
    // paused one has finished, gets the audio heard in between, and a final
    // from the paused attempt arriving later still is not counted twice.
    void pauseKeepsTheWordsAndResumesInAFreshAttempt()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setPauseMediaDuringTranscription(true);
        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("first"));
        speech->autoCompleteOnFinish = false;
        const quint64 pausedAttempt = speech->currentAttemptId;

        session.pause();
        QCOMPARE(int(session.state()), int(DictationState::Paused));
        QCOMPARE(speech->stopCalls, 1);
        QVERIFY(!audio->isActive());
        QCOMPARE(media->resumeCalls, 0);

        session.resume();
        QCOMPARE(int(session.state()), int(DictationState::Listening));
        QVERIFY(audio->isActive());
        QCOMPARE(speech->startCalls, 1);
        audio->pushAudio(QByteArrayLiteral("heard in between"));
        speech->emitFinalText(QStringLiteral("second"));
        speech->emitCompletion();
        QCOMPARE(speech->startCalls, 2);
        QVERIFY(speech->currentAttemptId != pausedAttempt);
        QCOMPARE(speech->audioChunks.last(), QByteArrayLiteral("heard in between"));
        emit speech->finalTranscript(pausedAttempt, QStringLiteral("second"));
        speech->emitFinalText(QStringLiteral("third"));
        speech->autoCompleteOnFinish = true;
        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 250);
        QCOMPARE(delivery->lastText, QStringLiteral("first second third"));
    }

    // Stop while paused delivers what was said, once the paused attempt's
    // words are in; the time spent paused is not counted as dictation.
    void stopWhilePausedDeliversTheWordsSoFar()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setInsightsEnabled(true);
        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);
        QSignalSpy recorded(&session, &DictationSession::dictationRecorded);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("before the pause"));
        speech->autoCompleteOnFinish = false;
        session.pause();
        QTest::qWait(400);
        session.stopListening();
        QCOMPARE(int(session.state()), int(DictationState::Stopping));
        QCOMPARE(delivery->calls, 0);

        speech->emitFinalText(QStringLiteral("and its tail"));
        speech->emitCompletion();
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 250);
        QCOMPARE(delivery->lastText, QStringLiteral("before the pause and its tail"));
        QCOMPARE(recorded.count(), 1);
        QVERIFY(recorded.first().first().value<DictationRecord>().audioMs < 400);
    }

    // The real microphone's stop() spins an event loop for the post-roll. A
    // completion and a stop that land inside a cancel's stop must find the
    // session already discarded, not deliver under its new generation.
    void cancelStaysCanceledWhenEventsLandInsideTheMicrophoneStop()
    {
        SettingsStore settings;
        settings.raw().clear();
        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("never delivered"));
        bool stopped = false;
        audio->onStop = [&] {
            if (std::exchange(stopped, true))
                return;
            speech->emitCompletion();
            session.stopListening();
        };
        session.cancel();
        QTest::qWait(10);

        QCOMPARE(int(session.state()), int(DictationState::Idle));
        QCOMPARE(delivery->calls, 0);
    }

    // Starting the microphone spins an event loop too: a stop that lands while
    // a resume starts it delivers, and the resume then leaves the session be.
    void stopDuringResumeDeliversAndLeavesTheSessionAlone()
    {
        SettingsStore settings;
        settings.raw().clear();
        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("before the pause"));
        session.pause();
        bool started = false;
        audio->onStart = [&] {
            if (!std::exchange(started, true))
                session.stopListening();
        };
        session.resume();

        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 250);
        QCOMPARE(delivery->lastText, QStringLiteral("before the pause"));
        QVERIFY(session.state() != DictationState::Listening);
        QVERIFY(!audio->isActive());
    }

    // Speech after a resume that comes before the paused attempt has finished
    // waits for it; a stop then gives it an attempt of its own before
    // refining, so none of it is lost.
    void stopAfterResumeKeepsTheWordsHeardSinceTheResume()
    {
        SettingsStore settings;
        settings.raw().clear();
        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("before"));
        speech->autoCompleteOnFinish = false;
        session.pause();
        session.resume();
        audio->pushAudio(QByteArrayLiteral("after"));
        session.stopListening();
        QCOMPARE(speech->stopCalls, 1);

        speech->emitCompletion();
        QCOMPARE(speech->startCalls, 2);
        QCOMPARE(speech->audioChunks.last(), QByteArrayLiteral("after"));
        QCOMPARE(delivery->calls, 0);
        speech->emitFinalText(QStringLiteral("after"));
        speech->emitCompletion();

        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 250);
        QCOMPARE(delivery->lastText, QStringLiteral("before after"));
    }

    // Paused, resumed briefly and paused again before the first pause's
    // attempt finished: that attempt ends while paused, and a stop still sends
    // the brief resume's audio to an attempt before refining.
    void stopAfterASecondPauseKeepsTheBriefResume()
    {
        SettingsStore settings;
        settings.raw().clear();
        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("before"));
        speech->autoCompleteOnFinish = false;
        session.pause();
        session.resume();
        audio->pushAudio(QByteArrayLiteral("brief"));
        session.pause();
        QCOMPARE(speech->stopCalls, 1);
        speech->emitCompletion();
        QCOMPARE(int(session.state()), int(DictationState::Paused));

        session.stopListening();
        QCOMPARE(speech->startCalls, 2);
        QCOMPARE(speech->audioChunks.last(), QByteArrayLiteral("brief"));
        speech->emitFinalText(QStringLiteral("brief"));
        speech->emitCompletion();

        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 250);
        QCOMPARE(delivery->lastText, QStringLiteral("before brief"));
    }

    // A start that lands inside a cancel's microphone stop waits for the stop.
    // A cancel before it runs drops it; otherwise the new session starts.
    void aStartDuringTheMicrophoneStopWaitsAndACancelDropsIt()
    {
        SettingsStore settings;
        settings.raw().clear();
        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);
        bool startInsideStop = true;
        audio->onStop = [&] {
            if (std::exchange(startInsideStop, false)) {
                session.startListening();
            }
        };

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        session.cancel();
        QVERIFY(session.startPending());
        QCOMPARE(session.stateName(), QStringLiteral("starting"));
        session.cancel();
        QTest::qWait(30);
        QCOMPARE(int(session.state()), int(DictationState::Idle));
        QVERIFY(!session.startPending());
        QCOMPARE(speech->startCalls, 1);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        startInsideStop = true;
        session.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        QCOMPARE(speech->startCalls, 3);
        session.cancel();
    }

    // The paused attempt can finish inside the stop's post-roll, with more
    // post-roll after it: all of it reaches one last attempt, finished once.
    void aPausedAttemptEndingInsideTheStopKeepsThePostRoll()
    {
        SettingsStore settings;
        settings.raw().clear();
        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("before"));
        speech->autoCompleteOnFinish = false;
        session.pause();
        session.resume();
        audio->pushAudio(QByteArrayLiteral("a"));
        bool stopped = false;
        audio->onStop = [&] {
            if (std::exchange(stopped, true)) {
                return;
            }
            speech->emitCompletion();
            audio->pushAudio(QByteArrayLiteral("b"));
        };
        session.stopListening();

        QCOMPARE(speech->startCalls, 2);
        QCOMPARE(speech->audioChunks, (QList<QByteArray>{QByteArrayLiteral("a"), QByteArrayLiteral("b")}));
        QCOMPARE(speech->stopCalls, 2);
        speech->emitFinalText(QStringLiteral("after"));
        speech->emitCompletion();
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 250);
        QCOMPARE(delivery->lastText, QStringLiteral("before after"));
    }

    // A microphone that will not restart after a pause lets go of the empty
    // attempt the resume opened and delivers what the pause kept.
    void aFailedRestartAfterAPauseDeliversWithoutAnEmptyAttempt()
    {
        SettingsStore settings;
        settings.raw().clear();
        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("kept"));
        session.pause();
        audio->startResult = false;
        session.resume();

        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 250);
        QCOMPARE(delivery->lastText, QStringLiteral("kept"));
        QCOMPARE(speech->stopCalls, 1);
        QCOMPARE(speech->cancelledAttempts.last(), speech->currentAttemptId);
    }

    // Every pause button calls togglePause: it pauses a listening session and
    // resumes a paused one.
    void togglePausePausesAndResumes()
    {
        SettingsStore settings;
        settings.raw().clear();
        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        session.togglePause();
        QCOMPARE(int(session.state()), int(DictationState::Paused));
        QVERIFY(!audio->isActive());
        session.togglePause();
        QCOMPARE(int(session.state()), int(DictationState::Listening));
        QVERIFY(audio->isActive());
        session.cancel();
    }

    // A paused attempt whose stream fails before it finishes may have lost its
    // last words, and the receipt says so.
    void aPausedAttemptThatFailsWarnsThatWordsMayBeMissing()
    {
        SettingsStore settings;
        settings.raw().clear();
        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);
        QSignalSpy message(&session, &DictationSession::popupMessageRequested);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("kept"));
        speech->autoCompleteOnFinish = false;
        session.pause();
        speech->emitFailure(QStringLiteral("stream closed"), true, QStringLiteral("streaming"));
        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(message.count(), 1, 250);
        QCOMPARE(message.first().at(0).toString(),
                 QStringLiteral("Used raw transcript • Input sent • Part of the dictation may be missing. "
                                "The connection dropped."));
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

#ifdef SPEECHER_WITH_WAYLAND
    void livePortalScreenshotCapture()
    {
        if (qEnvironmentVariableIsEmpty("SPEECHER_LIVE_SCREENSHOT_TEST")) {
            QSKIP("Set SPEECHER_LIVE_SCREENSHOT_TEST=1 inside a desktop session");
        }

        PortalScreenshotContextProvider screenshots;
        QSignalSpy captured(&screenshots, &PortalScreenshotContextProvider::captured);
        QSignalSpy failed(&screenshots, &PortalScreenshotContextProvider::failed);
        // The microphone is read on the main thread, so a capture that stalls
        // the event loop cuts out whatever the person says meanwhile.
        QElapsedTimer sinceTick;
        qint64 longestStallMs = 0;
        QTimer tick;
        connect(&tick, &QTimer::timeout, &tick, [&] {
            longestStallMs = std::max(longestStallMs, sinceTick.restart());
        });
        sinceTick.start();
        tick.start(10);
        screenshots.capture();

        QTRY_VERIFY_WITH_TIMEOUT(!captured.isEmpty() || !failed.isEmpty(), 15000);
        const QString failureMessage = failed.isEmpty()
            ? QString()
            : failed.first().first().toString();
        QVERIFY2(failed.isEmpty(), qPrintable(failureMessage));
        QVERIFY(captured.first().at(0).toByteArray().size() > 100);
        QCOMPARE(captured.first().at(1).toString(), QStringLiteral("image/png"));
        // A stall just before the result lands has no tick after it yet.
        longestStallMs = std::max(longestStallMs, sinceTick.elapsed());
        QVERIFY2(longestStallMs < 100,
                 qPrintable(QStringLiteral("event loop stalled for %1 ms").arg(longestStallMs)));
    }
#endif // SPEECHER_WITH_WAYLAND

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
            "popupErrorRequested(QString,speecher::PopupErrorAction)");
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
        QCOMPARE(message.first().at(1).value<PopupErrorAction>().pageId, QStringLiteral("accounts"));
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

    void transcriberPopupReplacesOAuthLabelWithPreview()
    {
        TranscriberPopup popup(new FakePopupPositioner);
        auto *previewPill = popup.findChild<QFrame *>(QStringLiteral("previewPill"));
        auto *rawTranscript = popup.findChild<QLabel *>(QStringLiteral("rawTranscript"));
        auto *waveform = popup.findChild<WaveformWidget *>();
        QVERIFY(previewPill);
        QVERIFY(rawTranscript);
        QVERIFY(waveform);
        QVERIFY(!popup.findChild<QLabel *>(QStringLiteral("popupStatus")));
        QVERIFY(!popup.findChild<QLabel *>(QStringLiteral("popupMetadata")));
        QCOMPARE(previewPill->minimumHeight(), waveform->height());
        QCOMPARE(previewPill->maximumHeight(), waveform->height());
        QVERIFY(!rawTranscript->wordWrap());

        popup.showOAuthRefreshIndicator();
        QVERIFY(rawTranscript->isHidden());
        QVERIFY(!waveform->isHidden());

        popup.setPreview(QStringLiteral("hello world"));
        QCOMPARE(rawTranscript->text(), QStringLiteral("hello world"));
        QVERIFY(!rawTranscript->isHidden());

        popup.showOAuthRefreshIndicator();
        popup.hidePreview();
        QVERIFY(rawTranscript->isHidden());
        QVERIFY(!previewPill->isHidden());
        QVERIFY(!waveform->isHidden());

        const QString longRaw = QStringLiteral(
            "one two three four five six seven eight nine ten eleven twelve");
        popup.setPreview(longRaw);
        QVERIFY(!rawTranscript->text().contains(QLatin1Char('\n')));
        popup.setRefining(true);
        QVERIFY(rawTranscript->isHidden());
        QVERIFY(!previewPill->isHidden());
        QVERIFY(!waveform->isHidden());
    }

    void transcriberPopupStreamsRefinementInsteadOfSpeechPreview()
    {
        TranscriberPopup popup(new FakePopupPositioner);
        auto *previewPill = popup.findChild<QFrame *>(QStringLiteral("previewPill"));
        auto *rawTranscript = popup.findChild<QLabel *>(QStringLiteral("rawTranscript"));
        auto *waveform = popup.findChild<WaveformWidget *>();
        QVERIFY(previewPill);
        QVERIFY(rawTranscript);
        QVERIFY(waveform);

        popup.setPreview(QStringLiteral("hello world"));
        QVERIFY(!previewPill->isHidden());

        // Mic toggled off: the speech preview disappears for the whole
        // transcribe-then-refine stretch, even when late partials arrive.
        popup.setSessionState(DictationState::Stopping);
        QVERIFY(!previewPill->isHidden());
        QVERIFY(rawTranscript->isHidden());
        QVERIFY(!waveform->isHidden());
        popup.setPreview(QStringLiteral("late speech words"));
        QVERIFY(!previewPill->isHidden());
        QVERIFY(rawTranscript->isHidden());
        QVERIFY(!waveform->isHidden());

        popup.setFrozen(true);
        popup.setRefining(true);
        QVERIFY(!previewPill->isHidden());
        QVERIFY(rawTranscript->isHidden());
        QVERIFY(!waveform->isHidden());

        popup.setRefinementPreview(QStringLiteral("Polished words"));
        QVERIFY(!previewPill->isHidden());
        QCOMPARE(rawTranscript->text(), QStringLiteral("Polished words"));
        popup.setPreview(QStringLiteral("speech again"));
        QCOMPARE(rawTranscript->text(), QStringLiteral("Polished words"));

        popup.setRefining(false);
        popup.showMessage(QStringLiteral("Input sent"), PopupOutcome::Inserted);
        QVERIFY(!previewPill->isHidden());
        QVERIFY(rawTranscript->isHidden());
        QVERIFY(!waveform->isHidden());

        // The next dictation session starts clean.
        popup.showListeningIndicator();
        popup.setPreview(QStringLiteral("fresh"));
        QCOMPARE(rawTranscript->text(), QStringLiteral("fresh"));
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

        // Clear of the test's pointer: resting on the capsule holds the countdown.
        popup.move(QCursor::pos() + QPoint(50, 50));
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
        QTRY_VERIFY_WITH_TIMEOUT(popup.isHidden(), popupErrorDismissMs(error) + 500);
    }
};

int runDictationSessionLifecycleTests(int argc, char **argv)
{
    DictationSessionLifecycleTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_dictation_session_lifecycle.moc"
