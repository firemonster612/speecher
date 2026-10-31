#include "common/test_prelude.h"
#include "common/test_doubles.h"
#include "common/audio_input_waits.h"
#include "providers/PreparationRunner.h"

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
        QCOMPARE(dictationStatusLabel(DictationState::Idle), QStringLiteral("Idle"));
        QCOMPARE(dictationStatusLabel(DictationState::Starting), QStringLiteral("Listening…"));
        QCOMPARE(dictationStatusLabel(DictationState::Listening), QStringLiteral("Listening…"));
        QCOMPARE(dictationStatusLabel(DictationState::Stopping), QStringLiteral("Transcribing…"));
        QCOMPARE(dictationStatusLabel(DictationState::Refining), QStringLiteral("Refining…"));
        QCOMPARE(dictationStatusLabel(DictationState::Delivering), QStringLiteral("Delivering…"));
        QCOMPARE(dictationStatusLabel(DictationState::Delivering, QStringLiteral("Copied")),
                 QStringLiteral("Copied"));
        QCOMPARE(dictationStatusLabel(DictationState::Error, QStringLiteral("Microphone unavailable")),
                 QStringLiteral("Microphone unavailable"));
        // A message outside delivering and error is the last session's.
        QCOMPARE(dictationStatusLabel(DictationState::Idle, QStringLiteral("Copied")), QStringLiteral("Idle"));

        QCOMPARE(dictationToggleAction(DictationState::Idle).label, QStringLiteral("Start dictation"));
        QCOMPARE(dictationToggleAction(DictationState::Listening).label, QStringLiteral("Stop dictation"));
        QCOMPARE(dictationToggleAction(DictationState::Refining).label, QStringLiteral("Cancel refinement"));
        QVERIFY(dictationToggleAction(DictationState::Refining).enabled);
        QVERIFY(!dictationToggleAction(DictationState::Stopping).enabled);
        QVERIFY(!dictationToggleAction(DictationState::Delivering).enabled);
        QVERIFY(dictationToggleAction(DictationState::Error).enabled);
        QCOMPARE(dictationStatusLabel(DictationState::Reviewing),
                 QStringLiteral("Review the edit in the popup"));
        QCOMPARE(dictationToggleAction(DictationState::Reviewing).label, QStringLiteral("Ask for more changes"));
        QVERIFY(dictationToggleAction(DictationState::Reviewing).enabled);
    }

    // The session shortcuts hold their keys only while their action applies:
    // cancel from Starting through Refining, pause while Listening or Paused.
    void sessionShortcutsApplyOnlyWhileTheirActionDoes()
    {
        for (const DictationState state : {DictationState::Listening, DictationState::Paused}) {
            QVERIFY(dictationPausable(state));
            QVERIFY(dictationCancelable(state));
        }
        for (const DictationState state : {DictationState::Starting, DictationState::Refining}) {
            QVERIFY(!dictationPausable(state));
            QVERIFY(dictationCancelable(state));
        }
        QVERIFY(!dictationPausable(DictationState::Idle));
        QVERIFY(!dictationCancelable(DictationState::Idle));
        // Cancel keeps the original while an edit is reviewed.
        QVERIFY(dictationCancelable(DictationState::Reviewing));
        QVERIFY(!dictationPausable(DictationState::Reviewing));
    }

    void sessionKeysAreTakenOnlyWhileTheyAct_data()
    {
        QTest::addColumn<DictationState>("state");
        QTest::addColumn<bool>("reviewUp");
        QTest::addColumn<bool>("escapeCancelsDictation");
        QTest::addColumn<QString>("cancelShortcut");
        QTest::addColumn<QString>("pauseShortcut");
        QTest::addColumn<bool>("suspended");
        QTest::addColumn<bool>("escape");
        QTest::addColumn<bool>("enter");
        const QString none;
        const QString escape = QStringLiteral("Esc");
        // Windows and macOS: Escape cancels from Starting through Refining.
        QTest::newRow("listening, Windows and macOS") << DictationState::Listening << false << true << none << none << false << true << false;
        QTest::newRow("idle") << DictationState::Idle << false << true << none << none << false << false << false;
        QTest::newRow("delivering") << DictationState::Delivering << false << true << none << none << false << false << false;
        QTest::newRow("Cancel Shortcut is Escape") << DictationState::Listening << false << true << escape << none << false << false << false;
        QTest::newRow("Pause Shortcut is Escape") << DictationState::Listening << false << true << none << escape << false << false << false;
        // Linux takes nothing while dictating.
        QTest::newRow("listening, Linux") << DictationState::Listening << false << false << none << none << false << false << false;
        // Waiting for review, every platform: Escape keeps and Enter replaces.
        QTest::newRow("reviewing, Linux") << DictationState::Reviewing << true << false << none << none << false << true << true;
        QTest::newRow("reviewing, Pause Shortcut is Escape") << DictationState::Reviewing << true << true << none << escape << false << true << true;
        QTest::newRow("reviewing, Cancel Shortcut is Escape") << DictationState::Reviewing << true << false << escape << none << false << false << true;
        QTest::newRow("reviewing, suspended") << DictationState::Reviewing << true << true << none << none << true << false << false;
        // Dictating a follow-up, every platform: Escape keeps the review
        // rather than reaching the Target; Enter is free for the dictation.
        QTest::newRow("following up, Linux") << DictationState::Listening << true << false << none << none << false << true << false;
        QTest::newRow("following up, Pause Shortcut is Escape") << DictationState::Listening << true << false << none << escape << false << false << false;
        QTest::newRow("following up, refining, Pause Shortcut is Escape") << DictationState::Refining << true << false << none << escape << false << true << false;
    }

    void sessionKeysAreTakenOnlyWhileTheyAct()
    {
        QFETCH(DictationState, state);
        QFETCH(bool, reviewUp);
        QFETCH(bool, escapeCancelsDictation);
        QFETCH(QString, cancelShortcut);
        QFETCH(QString, pauseShortcut);
        QFETCH(bool, suspended);
        QFETCH(bool, escape);
        QFETCH(bool, enter);
        const auto binding = [](const QString &keys) {
            return keys.isEmpty() ? ShortcutBinding() : ShortcutBinding(QKeySequence(keys));
        };
        QCOMPARE(sessionKeysWanted(state, reviewUp, escapeCancelsDictation, binding(cancelShortcut),
                                   binding(pauseShortcut), suspended),
                 (SessionKeys{escape, enter}));
    }

    // The review names only the keys that work: Escape where it is held, or
    // else the Cancel Shortcut; Enter where it is held.
    void reviewNamesTheKeysHeld()
    {
        QCOMPARE(reviewKeysFor(true, true, QString(), QStringLiteral("Meta+Space")),
                 (ReviewKeys{escapeKeyName(), enterKeyName(), QStringLiteral("Meta+Space")}));
        QCOMPARE(reviewKeysFor(false, false, QStringLiteral("Meta+C"), QString()),
                 (ReviewKeys{QStringLiteral("Meta+C"), QString(), QString()}));
        QCOMPARE(reviewKeysFor(false, false, QString(), QString()), ReviewKeys());
    }

    void popupErrorsStayLongEnoughToRead()
    {
        QCOMPARE(popupErrorDismissMs(QStringLiteral("Microphone unavailable")), 8000);
        QCOMPARE(popupErrorDismissMs(QString(200, u'a')), 12000);
        QCOMPARE(popupErrorDismissMs(QString(2000, u'a')), 30000);
    }

    // The last transcript is the preview, then the delivered text, and
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

    void preparationRunnerAppliesJobsInOrder()
    {
        PreparationRunner runner;
        QStringList events;
        PreparationJob speechJob;
        speechJob.run = [&events] {
            events.append(QStringLiteral("speech run"));
            return std::optional<ProviderFailure>();
        };
        speechJob.apply = [&events](const std::optional<ProviderFailure> &) {
            events.append(QStringLiteral("speech apply"));
        };
        PreparationJob refinerJob;
        refinerJob.run = [&events] {
            events.append(QStringLiteral("refiner run"));
            return std::optional<ProviderFailure>();
        };
        refinerJob.apply = [&events](const std::optional<ProviderFailure> &) {
            events.append(QStringLiteral("refiner apply"));
        };
        QSignalSpy prepared(&runner, &PreparationRunner::speechPrepared);

        runner.prepareSpeech(std::move(speechJob), std::move(refinerJob));

        QTRY_COMPARE_WITH_TIMEOUT(prepared.count(), 1, 1000);
        QCOMPARE(events, QStringList({QStringLiteral("speech run"),
                                      QStringLiteral("refiner run"),
                                      QStringLiteral("speech apply"),
                                      QStringLiteral("refiner apply")}));
        QVERIFY(!prepared.first().first().value<std::optional<ProviderFailure>>());
    }

    void preparationRunnerSkipsRefinerAfterSpeechFailure()
    {
        PreparationRunner runner;
        bool speechApplied = false;
        bool refinerRan = false;
        PreparationJob speechJob;
        speechJob.run = [] {
            return std::optional(ProviderFailure{ProviderFailureKind::Unavailable, QStringLiteral("speech failed")});
        };
        speechJob.apply = [&speechApplied](const std::optional<ProviderFailure> &) {
            speechApplied = true;
        };
        PreparationJob refinerJob;
        refinerJob.run = [&refinerRan] {
            refinerRan = true;
            return std::optional<ProviderFailure>();
        };
        QSignalSpy prepared(&runner, &PreparationRunner::speechPrepared);

        runner.prepareSpeech(std::move(speechJob), std::move(refinerJob));

        QTRY_COMPARE_WITH_TIMEOUT(prepared.count(), 1, 1000);
        QVERIFY(speechApplied);
        QVERIFY(!refinerRan);
        const auto failure = prepared.first().first().value<std::optional<ProviderFailure>>();
        QVERIFY(failure);
        QCOMPARE(failure->message, QStringLiteral("speech failed"));
    }

    void preparationRunnerReportsRefinerFailure()
    {
        PreparationRunner runner;
        bool refinerApplied = false;
        PreparationJob refinerJob;
        refinerJob.run = [] {
            return std::optional(ProviderFailure{ProviderFailureKind::Network, QStringLiteral("refresh failed")});
        };
        refinerJob.apply = [&refinerApplied](const std::optional<ProviderFailure> &failure) {
            refinerApplied = failure.has_value();
        };
        QSignalSpy prepared(&runner, &PreparationRunner::refinerPrepared);

        runner.prepareRefiner(std::move(refinerJob));

        QTRY_COMPARE_WITH_TIMEOUT(prepared.count(), 1, 1000);
        QVERIFY(refinerApplied);
        const auto failure = prepared.first().first().value<std::optional<ProviderFailure>>();
        QVERIFY(failure);
        QCOMPARE(failure->kind, ProviderFailureKind::Network);
        QCOMPARE(failure->message, QStringLiteral("refresh failed"));
    }

    void preparationRunnerCancellationSuppressesCompletion()
    {
        PreparationRunner runner;
        QSemaphore started;
        QSemaphore release;
        bool applied = false;
        PreparationJob speechJob;
        speechJob.run = [&started, &release] {
            started.release();
            release.acquire();
            return std::optional<ProviderFailure>();
        };
        speechJob.apply = [&applied](const std::optional<ProviderFailure> &) {
            applied = true;
        };
        QSignalSpy prepared(&runner, &PreparationRunner::speechPrepared);

        runner.prepareSpeech(std::move(speechJob), std::nullopt);
        QVERIFY(started.tryAcquire(1, 1000));
        runner.cancel();
        release.release();

        QTest::qWait(50);
        QCOMPARE(prepared.count(), 0);
        QVERIFY(!applied);
    }

    void preparationRunnerDestructionDoesNotWaitForBlockedJob()
    {
        const auto started = std::make_shared<QSemaphore>();
        const auto release = std::make_shared<QSemaphore>();
        auto runner = std::make_unique<PreparationRunner>();
        PreparationJob speechJob;
        speechJob.run = [started, release] {
            started->release();
            release->acquire();
            return std::optional<ProviderFailure>();
        };
        runner->prepareSpeech(std::move(speechJob), std::nullopt);
        QVERIFY(started->tryAcquire(1, 1000));

        QElapsedTimer elapsed;
        elapsed.start();
        runner.reset();

        QVERIFY(elapsed.elapsed() < 500);
        release->release();
    }

    // Each start replaces the one before, whatever kind of job either is.
    void preparationRunnerIgnoresReplacedPreparation()
    {
        PreparationRunner runner;
        QSemaphore started;
        QSemaphore release;
        bool staleApplied = false;
        PreparationJob staleJob;
        staleJob.run = [&started, &release] {
            started.release();
            release.acquire();
            return std::optional<ProviderFailure>();
        };
        staleJob.apply = [&staleApplied](const std::optional<ProviderFailure> &) {
            staleApplied = true;
        };
        QSignalSpy speechPrepared(&runner, &PreparationRunner::speechPrepared);
        QSignalSpy refinerPrepared(&runner, &PreparationRunner::refinerPrepared);

        runner.prepareSpeech(std::move(staleJob), std::nullopt);
        QVERIFY(started.tryAcquire(1, 1000));
        PreparationJob refinerJob;
        refinerJob.run = [] { return std::optional<ProviderFailure>(); };
        runner.prepareRefiner(std::move(refinerJob));
        release.release();

        QTRY_COMPARE_WITH_TIMEOUT(refinerPrepared.count(), 1, 1000);
        QVERIFY(!staleApplied);
        QTest::qWait(50);
        QCOMPARE(speechPrepared.count(), 0);
        QCOMPARE(refinerPrepared.count(), 1);
    }

    // The speech request carries only the terms for the Writing Profile the
    // target settles.
    void speechKeyTermsFollowTheWritingProfile()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.edit([](AppSettings &draft) {
            draft.refinement.providerId = QStringLiteral("none");
            draft.refinement.defaultWritingProfile = WritingProfile::Work;
            return true;
        });
        VocabularyEntry kubernetes{QStringLiteral("Kubernetes")};
        kubernetes.profiles = {WritingProfile::Work};
        VocabularyEntry lucia{QStringLiteral("Lúcia")};
        lucia.profiles = {WritingProfile::Personal};
        settings.edit([&](AppSettings &draft) { draft.vocabulary = {kubernetes, lucia}; return true; });

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
        settings.edit([](AppSettings &draft) {
            draft.ui.previewWords = 7;
            draft.ui.pauseMediaDuringTranscription = true;
            draft.refinement.providerId = QStringLiteral("none");
            return true;
        });
        settings.edit([](AppSettings &draft) { draft.vocabulary = {{QStringLiteral("Speecher")}}; return true; });

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
        // A save mid-dictation changes the next preview.
        settings.edit([](AppSettings &draft) { draft.ui.previewWords = 3; return true; });
        speech->emitPartialText(QStringLiteral("ten"));
        QCOMPARE(previewDisplay.last().first().toString(), QStringLiteral("eight nine ten"));
        speech->emitPartialText(QString());
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
        settings.edit([](AppSettings &draft) { draft.refinement.providerId = QStringLiteral("openai"); return true; });
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
        settings.edit([](AppSettings &draft) { draft.refinement.providerId = QStringLiteral("none"); return true; });

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

        QTRY_COMPARE(speech->audioChunks,
                     QList<QByteArray>({QByteArrayLiteral("pre-roll"), QByteArrayLiteral("post-roll")}));
    }

    void dictationSessionDoesNotResumeAfterCancellationInsideAudioStart()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.edit([](AppSettings &draft) { draft.refinement.providerId = QStringLiteral("none"); return true; });

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
        settings.edit([](AppSettings &draft) {
            draft.refinement.providerId = QStringLiteral("none");
            draft.output.completionStatusDurationMs = 900;
            return true;
        });

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        delivery->result = {
            true,
            DeliveryReceipt::VerifiedInTarget,
            false,
            QStringLiteral("Sent to the app"),
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

        QTRY_COMPARE(message.count(), 1);
        QCOMPARE(message.first().first().toString(), QStringLiteral("Sent to the app"));
        QCOMPARE(message.first().at(1).value<PopupOutcome>(), PopupOutcome::Inserted);
        QCOMPARE(status.last().first().toString(), QStringLiteral("Sent to the app"));
        QCOMPARE(state.last().first().value<DictationState>(), DictationState::Delivering);
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
        QCOMPARE(state.last().first().value<DictationState>(), DictationState::Idle);
        QCOMPARE(int(session.state()), int(DictationState::Idle));

        // A clipboard-only receipt reports itself as copied, not sent.
        delivery->result = {true, DeliveryReceipt::Copied, false, QStringLiteral("Copied")};
        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("copied dictation"));
        message.clear();
        session.stopListening();
        QTRY_COMPARE(message.count(), 1);
        QCOMPARE(message.first().at(1).value<PopupOutcome>(), PopupOutcome::Copied);
    }

    void dictationSessionToggleAndPushToTalkCommandsAreIdempotent()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.edit([](AppSettings &draft) {
            draft.refinement.providerId = QStringLiteral("none");
            draft.output.format = OutputFormat::PlainText;
            return true;
        });

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
        QCOMPARE(settings.dictationSnapshot().output.format, OutputFormat::PlainText);

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
        settings.edit([](AppSettings &draft) {
            draft.ui.pauseMediaDuringTranscription = true;
            draft.refinement.providerId = QStringLiteral("none");
            return true;
        });

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

    void stopDuringTargetCaptureEndsTheStart()
    {
        // KWin's active-window query waits in a nested event loop, where a
        // push-to-talk release stops the session the target is captured for.
        // Neither capture nor startup may read the session settings the stop
        // dropped.
        SettingsStore settings;
        settings.raw().clear();
        settings.edit([](AppSettings &draft) {
            draft.ui.pauseMediaDuringTranscription = true;
            draft.refinement.providerId = QStringLiteral("none");
            return true;
        });

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto target = std::make_unique<FakeTargetProvider>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), target.get(), delivery.get(), &registry);
        target->onCapture = [&session] { session.stopListening(); };

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(target->captureCalls, 1, 250);
        QCOMPARE(int(session.state()), int(DictationState::Idle));
        QVERIFY(!speech);
        QCOMPARE(media->pauseCalls, 0);
        QVERIFY(!audio->started);
    }

    void dictationSessionWaitsForProviderCompletion()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.edit([](AppSettings &draft) { draft.refinement.providerId = QStringLiteral("none"); return true; });

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
        settings.edit([](AppSettings &draft) { draft.refinement.providerId = QStringLiteral("none"); return true; });

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
        speech->emitFailure(QStringLiteral("stream closed"), true, SpeechPhase::Streaming);
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
        settings.edit([](AppSettings &draft) {
            draft.refinement.providerId = QStringLiteral("none");
            draft.output.format = OutputFormat::PlainText;
            return true;
        });

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
        QCOMPARE(settings.dictationSnapshot().output.format, OutputFormat::PlainText);
    }

    void dictationSessionCapturesTargetAtStart()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.edit([](AppSettings &draft) { draft.refinement.providerId = QStringLiteral("none"); return true; });
        // The person's own rule outranks Kate's built-in Code editor match.
        settings.edit([](AppSettings &draft) {
            draft.appRecognitionRules = {
                {QStringLiteral("org.kde.kate"), AppCategory::Terminal, std::nullopt},
            };
            return true;
        });

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto targetProvider = std::make_unique<FakeTargetProvider>();
        targetProvider->target.applicationId = QStringLiteral("org.kde.kate");
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
        QTRY_COMPARE(int(session.state()), int(DictationState::Listening));
        speech->emitFinalText(QStringLiteral("hello"));
        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 250);
        QCOMPARE(delivery->lastTarget.applicationId, QStringLiteral("org.kde.kate"));
        QCOMPARE(delivery->lastTarget.caretOffset, 42);
        QCOMPARE(delivery->lastTarget.category, AppCategory::Terminal);
    }

    void startupWaitsForATargetCapturedLater_data()
    {
        QTest::addColumn<bool>("stoppedMeanwhile");
        QTest::newRow("the answer comes") << false;
        QTest::newRow("a stop comes first") << true;
    }

    // A provider asking the compositor answers the capture later. The start
    // waits for it: speech and the microphone start only once the Target is
    // known, and a stop meanwhile ends the start, the late answer with it.
    void startupWaitsForATargetCapturedLater()
    {
        QFETCH(bool, stoppedMeanwhile);
        SettingsStore settings;
        settings.raw().clear();
        settings.edit([](AppSettings &draft) { draft.refinement.providerId = QStringLiteral("none"); return true; });
        FakeAudioInput audio;
        FakeMediaController media;
        FakeTargetProvider targetProvider;
        targetProvider.target.applicationId = QStringLiteral("org.kde.konsole");
        targetProvider.holdsCapture = true;
        FakeDelivery delivery;
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, &audio, &media, &targetProvider, &delivery, &registry);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(targetProvider.captureCalls, 1, 250);
        QTest::qWait(20);
        QCOMPARE(int(session.state()), int(DictationState::Starting));
        QVERIFY(!speech || speech->startCalls == 0);
        QVERIFY(!audio.started);
        if (stoppedMeanwhile) {
            session.stopListening();
            QCOMPARE(int(session.state()), int(DictationState::Idle));
        }

        targetProvider.answerCapture();
        if (stoppedMeanwhile) {
            QTest::qWait(20);
            QCOMPARE(int(session.state()), int(DictationState::Idle));
            QVERIFY(!speech || speech->startCalls == 0);
            QVERIFY(!audio.started);
            return;
        }
        QTRY_COMPARE(int(session.state()), int(DictationState::Listening));
        speech->emitFinalText(QStringLiteral("hello"));
        session.stopListening();
        QTRY_COMPARE_WITH_TIMEOUT(delivery.calls, 1, 250);
        QCOMPARE(delivery.lastTarget.applicationId, QStringLiteral("org.kde.konsole"));
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
        settings.edit([&](AppSettings &draft) {
            draft.refinement.providerId = QStringLiteral("none");
            draft.insightsEnabled = enabled;
            return true;
        });

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
        QTRY_COMPARE(delivery->answers, 1);

        QCOMPARE(recorded.count(), enabled && delivered ? 1 : 0);
        if (!recorded.isEmpty()) {
            const auto record = recorded.first().first().value<DictationRecord>();
            QCOMPARE(record.words, 3);
            QCOMPARE(record.appName, QStringLiteral("Kate"));
            QVERIFY(record.audioMs > 0);
        }
    }

    // The saved language is one Claude Voice lacks: once the target settles
    // which service runs, the popup says so and offers the Dictation page,
    // and nothing reaches the service. --language replaces it for one session.
    void aSpokenLanguageTheServiceLacksStopsTheDictation()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.edit([](AppSettings &draft) {
            draft.refinement.providerId = QStringLiteral("none");
            draft.speech.language = QStringLiteral("cy");
            return true;
        });
        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);
        QSignalSpy message(&session, &DictationSession::popupErrorRequested);

        session.startListening();
        QTRY_COMPARE(int(session.state()), int(DictationState::Error));
        QCOMPARE(message.count(), 1);
        QCOMPARE(message.first().first().toString(),
                 QStringLiteral("Fake Speech can't listen for Welsh. Choose another Spoken Language."));
        QCOMPARE(message.first().at(1).value<PopupErrorAction>().pageId, QStringLiteral("speech"));
        QCOMPARE(speech->startCalls, 0);

        session.stopListening();
        session.startListeningWith({std::nullopt, std::nullopt, QStringLiteral("de")});
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        QCOMPARE(speech->lastLanguage, QStringLiteral("de"));
        QCOMPARE(settings.dictationSnapshot().speech.language, QStringLiteral("cy"));
        settings.raw().clear();
    }

    void aForcedWritingProfileHoldsForTheWholeSession()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.edit([](AppSettings &draft) { draft.refinement.providerId = QStringLiteral("none"); return true; });
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
        QTRY_COMPARE(delivery->answers, 1);

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
        settings.edit([](AppSettings &draft) {
            draft.refinement.providerId = QStringLiteral("openai");
            draft.insightsEnabled = true;
            return true;
        });

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
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Reviewing), 1000);
        QCOMPARE(recorded.count(), 0);
        session.replaceSelection();
        QCOMPARE(delivery->calls, 1);
        QTRY_COMPARE(delivery->answers, 1);

        QCOMPARE(recorded.count(), 1);
        QCOMPARE(recorded.first().first().value<DictationRecord>().words, 3);
    }

    void turningInsightsOffMidSessionRecordsNothing()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.edit([](AppSettings &draft) {
            draft.refinement.providerId = QStringLiteral("none");
            draft.insightsEnabled = true;
            return true;
        });

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
        settings.edit([](AppSettings &draft) { draft.insightsEnabled = false; return true; });
        session.stopListening();
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);

        QCOMPARE(recorded.count(), 0);
    }

    void dictationSessionDefersTargetCaptureUntilPopupCanPaint()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.edit([](AppSettings &draft) { draft.refinement.providerId = QStringLiteral("none"); return true; });

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
        QTRY_COMPARE(int(session.state()), int(DictationState::Listening));
    }

    void dictationSessionCapturesOptionalScreenshotOnlyForRefinement()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.edit([](AppSettings &draft) { draft.refinement.providerId = QStringLiteral("openai"); return true; });

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

        settings.edit([](AppSettings &draft) { draft.refinement.includeScreenshotContext = true; return true; });
        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(screenshots->captureCalls, 1, 250);
        QTRY_COMPARE(int(session.state()), int(DictationState::Listening));
        speech->emitFinalText(QStringLiteral("second"));
        session.stopListening();
        QTRY_COMPARE_WITH_TIMEOUT(refiner->refineCalls, 2, 250);
        QCOMPARE(refiner->lastContext.screenshotData, screenshots->data);
        QCOMPARE(refiner->lastContext.screenshotMediaType, screenshots->mediaType);
        refiner->emitCompletedText(QStringLiteral("second"));
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 2, 250);
        QVERIFY(screenshots->cancelCalls >= 2);
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Idle), 1800);

        registerFakeRefiner(registry, &refiner, QStringLiteral("openai"), QStringLiteral("Fake Refiner"), false);
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
        settings.edit([](AppSettings &draft) {
            draft.ui.pauseMediaDuringTranscription = true;
            draft.refinement.providerId = QStringLiteral("openai");
            return true;
        });

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

    // Delivery answers later: the session shows Delivering until it does,
    // and only the answer brings the receipt.
    void theReceiptWaitsForTheDeliveryToAnswer()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.edit([](AppSettings &draft) {
            draft.refinement.providerId = QStringLiteral("none");
            return true;
        });
        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        delivery->holdsAnswer = true;
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);
        QSignalSpy delivered(&session, &DictationSession::transcriptDelivered);
        QSignalSpy message(&session, &DictationSession::popupMessageRequested);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("on its way"));
        session.stopListening();
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);
        QTest::qWait(10);
        QCOMPARE(int(session.state()), int(DictationState::Delivering));
        QCOMPARE(delivered.count(), 0);
        QCOMPARE(message.count(), 0);

        delivery->answer();
        QCOMPARE(delivered.count(), 1);
        QCOMPARE(message.count(), 1);
        QCOMPARE(message.first().first().toString(), QStringLiteral("Sent to the app"));
    }

    // The shortcut waits while the words are on their way, and once they
    // have arrived starts the next dictation over the receipt.
    void aToggleStartsTheNextDictationOnceTheDeliveryHasAnswered()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.edit([](AppSettings &draft) {
            draft.refinement.providerId = QStringLiteral("none");
            return true;
        });
        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        delivery->holdsAnswer = true;
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("first"));
        session.stopListening();
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);
        session.toggle();
        QCOMPARE(int(session.state()), int(DictationState::Delivering));

        delivery->answer();
        QCOMPARE(int(session.state()), int(DictationState::Delivering));
        session.toggle();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
    }

    // A quit as the session turns to Delivering sends nothing.
    void cancelForShutdownOnDeliveringSendsNothing()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.edit([](AppSettings &draft) {
            draft.refinement.providerId = QStringLiteral("none");
            return true;
        });
        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);
        connect(&session, &DictationSession::stateChanged, &session, [&session](DictationState state) {
            if (state == DictationState::Delivering) session.cancelForShutdown();
        });

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("quit must not paste this"));
        session.stopListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Idle), 1000);
        QTest::qWait(10);
        QCOMPARE(delivery->calls, 0);
    }

    // Quitting while the words are on their way: the delivery finishes, and
    // its answer brings no receipt, even once another dictation delivers.
    void cancelForShutdownWhileDeliveringDropsTheAnswer()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.edit([](AppSettings &draft) {
            draft.refinement.providerId = QStringLiteral("none");
            return true;
        });
        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        delivery->holdsAnswer = true;
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);
        QSignalSpy message(&session, &DictationSession::popupMessageRequested);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("quit while pasting"));
        session.stopListening();
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);
        session.cancelForShutdown();
        QCOMPARE(int(session.state()), int(DictationState::Idle));

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("after the quit"));
        session.stopListening();
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 2, 1000);
        delivery->answer();
        QCOMPARE(message.count(), 0);
        QCOMPARE(int(session.state()), int(DictationState::Delivering));
        delivery->answer();
        QCOMPARE(message.count(), 1);
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
            settings.edit([](AppSettings &draft) {
                draft.ui.pauseMediaDuringTranscription = true;
                draft.insightsEnabled = true;
                draft.refinement.providerId = QStringLiteral("openai");
                draft.output.completionStatusDurationMs = 300;
                return true;
            });
            auto audio = std::make_unique<FakeAudioInput>();
            auto media = std::make_unique<FakeMediaController>();
            auto delivery = std::make_unique<FakeDelivery>();
            ProviderRegistry registry;
            FakeSpeechTranscriber *speech = nullptr;
            FakeRefiner *refiner = nullptr;
            registerFakeSpeechProvider(registry, &speech);
            registerFakeRefiner(registry, &refiner);
            // Made now: a session canceled while starting never got as far
            // as asking for them, and the late events below still need them.
            registry.speechProvider(QStringLiteral("claude"));
            registry.refinementProvider(QStringLiteral("openai"));
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

            QElapsedTimer sinceCancel;
            qint64 hiddenAfterMs = -1;
            connect(&session, &DictationSession::popupHideRequested, &session,
                    [&] { hiddenAfterMs = sinceCancel.elapsed(); });
            sinceCancel.start();
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
            // "Canceled" stays up for the result duration setting, timed when
            // it went rather than by a wait, which load can stretch. Less 50 ms
            // of slack for the timer, which still fails a hide straight away.
            if (target == DictationState::Listening) {
                QTRY_COMPARE(hidden.count(), 1);
                QVERIFY2(hiddenAfterMs >= settings.dictationSnapshot().output.completionStatusDurationMs - 50,
                         qPrintable(QString::number(hiddenAfterMs)));
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

        speech->prepareFailure = ProviderFailure{ProviderFailureKind::Unavailable, QStringLiteral("Sign in again")};
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
        settings.edit([](AppSettings &draft) { draft.ui.pauseMediaDuringTranscription = true; return true; });
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

        QVERIFY(pauseAndWait(session));
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

    void audioPastTheConnectBudgetNeverCutsAPausedAttemptShort_data()
    {
        QTest::addColumn<bool>("stoppedFirst");
        QTest::newRow("still listening") << false;
        QTest::newRow("stopping") << true;
    }

    // Audio heard after a resume while the paused attempt still finishes is
    // held up to the connect budget's worth, ten seconds. From the chunk that
    // goes past it on, audio is dropped, even a smaller one that would fit,
    // and listening stops; but the paused attempt, whose words may only come
    // when it finishes, is never cancelled: it finishes, and what is held
    // goes to one final attempt on the same provider.
    void audioPastTheConnectBudgetNeverCutsAPausedAttemptShort()
    {
        QFETCH(bool, stoppedFirst);
        SettingsStore settings;
        settings.raw().clear();
        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);
        QSignalSpy outcome(&session, &DictationSession::popupMessageRequested);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->autoCompleteOnFinish = false;
        const quint64 pausedAttempt = speech->currentAttemptId;
        QVERIFY(pauseAndWait(session));
        session.resume();
        if (stoppedFirst) {
            session.stopListening();
            QCOMPARE(int(session.state()), int(DictationState::Stopping));
        }
        const QByteArray second(16000 * 2, '\0');
        const QByteArray halfSecond(16000, '\0');
        QList<QByteArray> held(9, second);
        held << halfSecond;
        for (const QByteArray &chunk : held) {
            audio->pushAudio(chunk);
        }
        audio->pushAudio(second);
        audio->pushAudio(QByteArray(8000, '\0'));
        QCOMPARE(int(session.state()), int(DictationState::Stopping));
        QVERIFY(speech->cancelledAttempts.isEmpty());
        QCOMPARE(delivery->calls, 0);

        emit speech->finalTranscript(pausedAttempt, QStringLiteral("said before the pause"));
        speech->autoCompleteOnFinish = true;
        speech->emitCompletion();

        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);
        QCOMPARE(delivery->lastText, QStringLiteral("said before the pause"));
        QCOMPARE(speech->startCalls, 2);
        QCOMPARE(speech->audioChunks, held);
        QVERIFY(speech->cancelledAttempts.isEmpty());
        QTRY_COMPARE(outcome.size(), 1);
        QVERIFY2(outcome.first().at(0).toString().contains(QStringLiteral("Part of the dictation may be missing")),
                 qPrintable(outcome.first().at(0).toString()));
        QVERIFY2(!outcome.first().at(0).toString().contains(QStringLiteral("too long")),
                 qPrintable(outcome.first().at(0).toString()));
    }

    // A second pause whose post-roll goes past the held-audio budget stops
    // listening inside its own microphone stop. When the paused attempt and
    // the final one both end inside that stop, the words are still delivered.
    void aSecondPauseGoingPastTheBudgetStillDelivers()
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
        speech->autoCompleteOnFinish = false;
        const quint64 pausedAttempt = speech->currentAttemptId;
        QVERIFY(pauseAndWait(session));
        session.resume();
        const QByteArray second(16000 * 2, '\0');
        for (int seconds = 0; seconds < 10; ++seconds) {
            audio->pushAudio(second);
        }
        bool overflowed = false;
        audio->onStop = [&] {
            if (std::exchange(overflowed, true)) {
                return;
            }
            audio->pushAudio(second);
            emit speech->finalTranscript(pausedAttempt, QStringLiteral("said before the pause"));
            speech->autoCompleteOnFinish = true;
            speech->emitCompletion();
        };
        session.pause();

        QTRY_VERIFY(overflowed);
        QCOMPARE(speech->startCalls, 2);
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);
        QCOMPARE(delivery->lastText, QStringLiteral("said before the pause"));
        QCOMPARE(speech->audioChunks, QList<QByteArray>(10, second));
    }

    // A completion inside a second pause's microphone stop, whose post-roll
    // went past the budget and stopped listening, waits for that stop: the
    // post-roll after the budget never reaches the attempt that takes what
    // was held.
    void aCompletionInsideAPausesStopPastTheBudgetKeepsLaterPostRollOut()
    {
        SettingsStore settings;
        settings.raw().clear();
        FakeAudioInput audio;
        FakeMediaController media;
        FakeDelivery delivery;
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, &audio, &media, &delivery, &registry);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->autoCompleteOnFinish = false;
        const quint64 pausedAttempt = speech->currentAttemptId;
        QVERIFY(pauseAndWait(session));
        session.resume();
        const QByteArray second(16000 * 2, '\0');
        for (int seconds = 0; seconds < 10; ++seconds) {
            audio.pushAudio(second);
        }
        int stops = 0;
        audio.onStop = [&] {
            ++stops;
            audio.pushAudio(second);
            audio.pushAudio(QByteArrayLiteral("tail"));
            emit speech->finalTranscript(pausedAttempt, QStringLiteral("said before the pause"));
            speech->emitCompletion();
        };
        session.pause();

        QTRY_COMPARE(int(session.state()), int(DictationState::Stopping));
        QTRY_VERIFY(!audio.active);
        QCOMPARE(stops, 1);
        QCOMPARE(speech->startCalls, 2);
        QCOMPARE(speech->audioChunks, QList<QByteArray>(10, second));
        speech->emitCompletion();
        QTRY_COMPARE_WITH_TIMEOUT(delivery.calls, 1, 250);
        QCOMPARE(delivery.lastText, QStringLiteral("said before the pause"));
    }

    // A session started as a cancel announces Idle waits for the cancel to
    // stop the microphone, then listens with it on.
    void aStartAsACancelEndsListensOnceTheMicrophoneStopped()
    {
        SettingsStore settings;
        settings.raw().clear();
        FakeAudioInput audio;
        FakeMediaController media;
        FakeDelivery delivery;
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, &audio, &media, &delivery, &registry);
        // A popup that paints at once lets the start go on in the same call.
        connect(&session, &DictationSession::popupShowRequested, &session,
                [&session](quint64 generation) { session.popupPresented(generation); });
        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        bool restarted = false;
        connect(&session, &DictationSession::stateChanged, &session, [&](DictationState state) {
            if (state == DictationState::Idle && !std::exchange(restarted, true)) {
                session.startListening();
            }
        });

        session.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);

        QVERIFY(restarted);
        QVERIFY(audio.active);
    }

    // A handler of Stopping that ends the session and starts another, and
    // waits for it to listen, leaves the new one's microphone on.
    void aRestartAsStoppingIsAnnouncedKeepsItsMicrophone_data()
    {
        QTest::addColumn<bool>("shutdown");
        QTest::newRow("cancel") << false;
        QTest::newRow("shutdown") << true;
    }

    void aRestartAsStoppingIsAnnouncedKeepsItsMicrophone()
    {
        QFETCH(bool, shutdown);
        SettingsStore settings;
        settings.raw().clear();
        FakeAudioInput audio;
        FakeMediaController media;
        FakeDelivery delivery;
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, &audio, &media, &delivery, &registry);
        connect(&session, &DictationSession::popupShowRequested, &session,
                [&session](quint64 generation) { session.popupPresented(generation); });
        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        bool restarted = false;
        connect(&session, &DictationSession::stateChanged, &session, [&](DictationState state) {
            if (state != DictationState::Stopping || std::exchange(restarted, true)) {
                return;
            }
            shutdown ? session.cancelForShutdown() : session.cancel();
            session.startListening();
            QVERIFY(QTest::qWaitFor([&] { return session.state() == DictationState::Listening; }, 250));
        });

        session.stopListening();

        QVERIFY(restarted);
        QCOMPARE(int(session.state()), int(DictationState::Listening));
        QVERIFY(audio.active);
    }

    // A speech failure as Stopping is announced moves the same session on,
    // to delivery: the stop still stops its microphone.
    void aSpeechFailureAsStoppingIsAnnouncedStillStopsTheMicrophone()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.edit([](AppSettings &draft) { draft.refinement.providerId = QStringLiteral("none"); return true; });
        FakeAudioInput audio;
        FakeMediaController media;
        FakeDelivery delivery;
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, &audio, &media, &delivery, &registry);
        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("heard"));
        bool failed = false;
        connect(&session, &DictationSession::stateChanged, &session, [&](DictationState state) {
            if (state == DictationState::Stopping && !std::exchange(failed, true)) {
                speech->emitFailure(QStringLiteral("dropped"));
            }
        });

        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(delivery.calls, 1, 250);
        QVERIFY(failed);
        QTRY_VERIFY(!audio.active);
        QCOMPARE(delivery.lastText, QStringLiteral("heard"));
    }

    // A cancel stops the microphone without a word, so nothing can cancel
    // again and start another session in the middle of it: an Idle session
    // never keeps capturing.
    void aCancelLeavesNoCaptureBehindItsIdle()
    {
        SettingsStore settings;
        settings.raw().clear();
        FakeAudioInput audio;
        FakeMediaController media;
        FakeDelivery delivery;
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, &audio, &media, &delivery, &registry);
        connect(&session, &DictationSession::popupShowRequested, &session,
                [&session](quint64 generation) { session.popupPresented(generation); });
        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        bool armed = true;
        connect(&session, &DictationSession::audioLevelChanged, &session, [&](float level) {
            if (level == 0.0f && std::exchange(armed, false)) {
                session.cancel();
                session.startListening();
            }
        });

        session.cancel();
        // Whatever the handler started has had its turns to reach the
        // microphone.
        QTest::qWait(100);

        QVERIFY(session.state() != DictationState::Idle || !audio.active);
    }

    // The pause button pressed again while the pause's microphone still
    // stops: the session never pauses, and listens on with the microphone on.
    void aSecondPauseToggleBeforeThePauseTakesListensOn()
    {
        SettingsStore settings;
        settings.raw().clear();
        FakeAudioInput audio;
        FakeMediaController media;
        FakeDelivery delivery;
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, &audio, &media, &delivery, &registry);
        QSignalSpy states(&session, &DictationSession::stateChanged);
        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);

        session.togglePause();
        session.togglePause();
        // The fake reports a turn later; this is ample for a pause to have
        // taken.
        QTest::qWait(50);

        QCOMPARE(int(session.state()), int(DictationState::Listening));
        QVERIFY(audio.active);
        for (const QList<QVariant> &state : states) {
            QVERIFY(state.first().value<DictationState>() != DictationState::Paused);
        }
    }

    // So does one started as a microphone that failed before anything was
    // heard ends the session in Error.
    void aStartAsAFailureEndsListensOnceTheMicrophoneStopped()
    {
        SettingsStore settings;
        settings.raw().clear();
        FakeAudioInput audio;
        FakeMediaController media;
        FakeDelivery delivery;
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, &audio, &media, &delivery, &registry);
        connect(&session, &DictationSession::popupShowRequested, &session,
                [&session](quint64 generation) { session.popupPresented(generation); });
        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        bool restarted = false;
        connect(&session, &DictationSession::stateChanged, &session, [&](DictationState state) {
            if (state == DictationState::Error && !std::exchange(restarted, true)) {
                session.startListening();
            }
        });

        audio.emitFailure(QStringLiteral("unplugged"));
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);

        QVERIFY(restarted);
        QVERIFY(audio.active);
    }

    // A handler that stops, or pauses, as the session first says it listens
    // finds the speech listening.
    void actingOnTheFirstListeningStillDelivers_data()
    {
        QTest::addColumn<bool>("pausesFirst");
        QTest::newRow("stop") << false;
        QTest::newRow("pause, then stop") << true;
    }

    void actingOnTheFirstListeningStillDelivers()
    {
        QFETCH(bool, pausesFirst);
        SettingsStore settings;
        settings.raw().clear();
        FakeAudioInput audio;
        FakeMediaController media;
        FakeDelivery delivery;
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, &audio, &media, &delivery, &registry);
        bool acted = false;
        connect(&session, &DictationSession::stateChanged, &session, [&](DictationState state) {
            if (state != DictationState::Listening || std::exchange(acted, true)) {
                return;
            }
            speech->emitFinalText(QStringLiteral("heard"));
            pausesFirst ? session.pause() : session.stopListening();
        });

        session.startListening();
        QTRY_VERIFY_WITH_TIMEOUT(acted, 250);
        if (pausesFirst) {
            // Paused once the microphone has stopped, a turn of the loop later.
            QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Paused), 250);
            session.stopListening();
        }
        QTRY_COMPARE_WITH_TIMEOUT(delivery.calls, 1, 250);
        QCOMPARE(delivery.lastText, QStringLiteral("heard"));
        QCOMPARE(speech->stopCalls, 1);
    }

    // As the session says it is paused, the speech already is: a stop then
    // delivers, a failure of the paused attempt leaves it paused, and its
    // completion opens no attempt.
    void actingOnPausedFindsTheSpeechPaused_data()
    {
        QTest::addColumn<QString>("action");
        QTest::newRow("stop") << QStringLiteral("stop");
        QTest::newRow("failure") << QStringLiteral("failure");
        QTest::newRow("completion") << QStringLiteral("completion");
    }

    void actingOnPausedFindsTheSpeechPaused()
    {
        QFETCH(QString, action);
        SettingsStore settings;
        settings.raw().clear();
        FakeAudioInput audio;
        FakeMediaController media;
        FakeDelivery delivery;
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, &audio, &media, &delivery, &registry);
        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("heard"));
        speech->autoCompleteOnFinish = action == QStringLiteral("stop");
        QSignalSpy statuses(&session, &DictationSession::statusChanged);
        connect(&session, &DictationSession::stateChanged, &session, [&](DictationState state) {
            if (state != DictationState::Paused) {
                return;
            }
            if (action == QStringLiteral("stop")) {
                session.stopListening();
            } else if (action == QStringLiteral("failure")) {
                speech->emitFailure(QStringLiteral("closed"));
            } else {
                speech->emitCompletion();
            }
        });

        session.pause();

        if (action == QStringLiteral("stop")) {
            QTRY_COMPARE_WITH_TIMEOUT(delivery.calls, 1, 250);
            QTRY_COMPARE(delivery.answers, 1);
            QCOMPARE(delivery.lastText, QStringLiteral("heard"));
            // The receipt is the last word; Paused is not shown after it.
            QCOMPARE(statuses.last().first().toString(), session.lastMessage());
            return;
        }
        QTRY_COMPARE(int(session.state()), int(DictationState::Paused));
        QCOMPARE(speech->startCalls, 1);
        QCOMPARE(delivery.calls, 0);
    }

    // Words the paused attempt sends as it finishes reach handlers once the
    // session shows Paused: a resume from one is not lost.
    void aResumeFromTheLastWordsOfAPausedAttemptListensAgain()
    {
        SettingsStore settings;
        settings.raw().clear();
        FakeAudioInput audio;
        FakeMediaController media;
        FakeDelivery delivery;
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, &audio, &media, &delivery, &registry);
        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->onFinishInput = [&] { speech->emitFinalText(QStringLiteral("last words")); };
        connect(&session, &DictationSession::previewChanged, &session, [&](const QString &text) {
            if (text == QStringLiteral("last words")) {
                session.resume();
            }
        });

        session.pause();

        QTRY_VERIFY(audio.isActive());
        QCOMPARE(int(session.state()), int(DictationState::Listening));
    }

    // The partial a resume commits as its attempt opens reaches handlers once
    // the session listens: a pause from one stops the microphone.
    void aPauseFromThePartialAResumeCommitsPauses()
    {
        SettingsStore settings;
        settings.raw().clear();
        FakeAudioInput audio;
        FakeMediaController media;
        FakeDelivery delivery;
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, &audio, &media, &delivery, &registry);
        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->ignoresEndedAttempts = true;
        speech->emitPartialText(QStringLiteral("unfinished"));
        QVERIFY(pauseAndWait(session));
        bool armed = true;
        connect(&session, &DictationSession::previewChanged, &session, [&] {
            if (std::exchange(armed, false)) {
                session.pause();
            }
        });

        session.resume();

        QVERIFY(!armed);
        QCOMPARE(int(session.state()), int(DictationState::Paused));
        QVERIFY(!audio.isActive());
        // The pause told the attempt the resume opened, so a stop still ends.
        session.stopListening();
        QTRY_COMPARE_WITH_TIMEOUT(delivery.calls, 1, 250);
        QCOMPARE(delivery.lastText, QStringLiteral("unfinished"));
    }

    // A handler that pauses and resumes again as a resume announces Listening
    // has the microphone started once, by its own resume.
    void aResumeOvertakenByAnotherStartsTheMicrophoneOnce()
    {
        SettingsStore settings;
        settings.raw().clear();
        FakeAudioInput audio;
        FakeMediaController media;
        FakeDelivery delivery;
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, &audio, &media, &delivery, &registry);
        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        QVERIFY(pauseAndWait(session));
        int starts = 0;
        audio.onStart = [&] { ++starts; };
        bool armed = true;
        connect(&session, &DictationSession::stateChanged, &session, [&](DictationState state) {
            if (state == DictationState::Listening && std::exchange(armed, false)) {
                session.pause();
                session.resume();
            }
        });

        session.resume();

        QCOMPARE(int(session.state()), int(DictationState::Listening));
        QTRY_COMPARE(starts, 1);
        // A second start would report on the fake's next turn; 20 ms gives it
        // several.
        QTest::qWait(20);
        QCOMPARE(starts, 1);
    }

    // A pause and resume from the partial the next attempt commits as it
    // opens finish that attempt: audio held for it waits for the one after.
    void heldAudioWaitsOutAPauseAndResumeFromACommittedPartial()
    {
        SettingsStore settings;
        settings.raw().clear();
        FakeAudioInput audio;
        FakeMediaController media;
        FakeDelivery delivery;
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, &audio, &media, &delivery, &registry);
        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->ignoresEndedAttempts = true;
        speech->autoCompleteOnFinish = false;
        speech->emitPartialText(QStringLiteral("unfinished"));
        QVERIFY(pauseAndWait(session));
        session.resume();
        audio.pushAudio(QByteArrayLiteral("held"));
        bool armed = true;
        connect(&session, &DictationSession::previewChanged, &session, [&] {
            if (std::exchange(armed, false)) {
                session.pause();
                session.resume();
            }
        });

        speech->emitCompletion();
        QVERIFY(!armed);
        speech->emitCompletion();

        QCOMPARE(speech->audioChunks, QList<QByteArray>({QByteArrayLiteral("held")}));
    }

    // A stop from the partial the next attempt commits as it opens: the
    // audio held for that attempt reaches it before the stop's post-roll.
    void heldAudioPrecedesPostRollAfterAStopFromACommittedPartial()
    {
        SettingsStore settings;
        settings.raw().clear();
        FakeAudioInput audio;
        FakeMediaController media;
        FakeDelivery delivery;
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, &audio, &media, &delivery, &registry);
        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->autoCompleteOnFinish = false;
        speech->emitPartialText(QStringLiteral("unfinished"));
        QVERIFY(pauseAndWait(session));
        session.resume();
        audio.pushAudio(QByteArrayLiteral("first"));
        audio.onStop = [&] { audio.pushAudio(QByteArrayLiteral("post")); };
        bool armed = true;
        connect(&session, &DictationSession::previewChanged, &session, [&] {
            if (std::exchange(armed, false)) {
                session.stopListening();
            }
        });

        speech->emitCompletion();
        QVERIFY(!armed);

        QTRY_COMPARE(speech->audioChunks, (QList<QByteArray>{QByteArrayLiteral("first"), QByteArrayLiteral("post")}));
        speech->emitCompletion();
        QTRY_COMPARE_WITH_TIMEOUT(delivery.calls, 1, 250);
        QCOMPARE(delivery.lastText, QStringLiteral("unfinished"));
    }

    // The empty attempt a failed restart leaves is let go before the session
    // shows Stopping: what its provider sends then does not count.
    void anAttemptLetGoAfterAFailedRestartIsNotHeardFromAgain()
    {
        SettingsStore settings;
        settings.raw().clear();
        FakeAudioInput audio;
        FakeMediaController media;
        FakeDelivery delivery;
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, &audio, &media, &delivery, &registry);
        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("kept"));
        QVERIFY(pauseAndWait(session));
        audio.startResult = false;
        connect(&session, &DictationSession::stateChanged, &session, [&](DictationState state) {
            if (state == DictationState::Stopping) {
                speech->emitFinalText(QStringLiteral("late"));
                speech->emitFailure(QStringLiteral("lost"), false, SpeechPhase::Finalize);
            }
        });

        session.resume();

        QTRY_COMPARE_WITH_TIMEOUT(delivery.calls, 1, 250);
        QCOMPARE(delivery.lastText, QStringLiteral("kept"));
        QVERIFY2(!session.lastMessage().contains(QStringLiteral("may be missing")), qPrintable(session.lastMessage()));
    }

    // A resume whose attempt fails as it opens delivers the words the pause
    // kept, without starting the microphone again.
    void aResumeFailingAtOnceDeliversWithoutTheMicrophone()
    {
        SettingsStore settings;
        settings.raw().clear();
        FakeAudioInput audio;
        FakeMediaController media;
        FakeDelivery delivery;
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, &audio, &media, &delivery, &registry);
        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("kept"));
        QVERIFY(pauseAndWait(session));
        speech->onStartAttempt = [&] { speech->emitFailure(QStringLiteral("refused")); };
        audio.started = false;

        session.resume();

        QTRY_COMPARE_WITH_TIMEOUT(delivery.calls, 1, 250);
        QCOMPARE(delivery.lastText, QStringLiteral("kept"));
        QVERIFY(!audio.started);
    }

    // Stop while paused delivers what was said, once the paused attempt's
    // words are in; the time spent paused is not counted as dictation.
    void stopWhilePausedDeliversTheWordsSoFar()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.edit([](AppSettings &draft) { draft.insightsEnabled = true; return true; });
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
        QVERIFY(pauseAndWait(session));
        QTest::qWait(400);
        session.stopListening();
        QCOMPARE(int(session.state()), int(DictationState::Stopping));
        QCOMPARE(delivery->calls, 0);

        speech->emitFinalText(QStringLiteral("and its tail"));
        speech->emitCompletion();
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 250);
        QTRY_COMPARE(delivery->answers, 1);
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
        QVERIFY(pauseAndWait(session));
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
        QVERIFY(pauseAndWait(session));
        session.resume();
        audio->pushAudio(QByteArrayLiteral("after"));
        session.stopListening();
        QCOMPARE(speech->stopCalls, 1);
        QTRY_VERIFY(!audio->isActive());

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
        QVERIFY(pauseAndWait(session));
        session.resume();
        audio->pushAudio(QByteArrayLiteral("brief"));
        QVERIFY(pauseAndWait(session));
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

    // A cancel stops the microphone at once, so a start right after it
    // begins straight away and listens.
    void aStartRightAfterACancelBeginsAtOnce()
    {
        SettingsStore settings;
        settings.raw().clear();
        FakeAudioInput audio;
        FakeMediaController media;
        FakeDelivery delivery;
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, &audio, &media, &delivery, &registry);
        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);

        session.cancel();
        QVERIFY(!audio.active);
        session.startListening();

        QCOMPARE(int(session.state()), int(DictationState::Starting));
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        QVERIFY(audio.active);
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
        QVERIFY(pauseAndWait(session));
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
        QTRY_VERIFY(!audio->isActive());

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
        QVERIFY(pauseAndWait(session));
        audio->startResult = false;
        session.resume();

        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 250);
        QCOMPARE(delivery->lastText, QStringLiteral("kept"));
        QCOMPARE(speech->stopCalls, 1);
        QCOMPARE(speech->cancelledAttempts.last(), speech->currentAttemptId);
    }

    // A recording beside a dictation hears the call whenever the dictation
    // does not hold the microphone: the dictation holds it from the start's
    // request, before the microphone starts, until it has stopped, post-roll
    // and all, but not while paused. It listens from its first Listening
    // until it stops, through a pause; a start whose microphone failed, or
    // that was stopped before it, never listens.
    void theMicrophoneIsTheDictationsOnlyWhileItListens()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.edit([](AppSettings &draft) { draft.refinement.providerId = QStringLiteral("none"); return true; });
        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);
        QSignalSpy microphone(&session, &DictationSession::microphoneChanged);
        QSignalSpy listening(&session, &DictationSession::listeningChanged);
        const auto values = [](const QSignalSpy &spy) {
            QList<bool> values;
            for (const QList<QVariant> &arguments : spy) {
                values << arguments.first().toBool();
            }
            return values;
        };
        // Whether the speech provider had been set up when the dictation
        // took the microphone.
        QList<bool> preparedAtHolds;
        connect(&session, &DictationSession::microphoneChanged, this, [&](bool held) {
            if (held) {
                preparedAtHolds << (speech != nullptr);
            }
        });
        QList<bool> heldAtStops;
        audio->onStop = [&] { heldAtStops << values(microphone).last(); };

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("hello"));
        QVERIFY(pauseAndWait(session));
        session.resume();
        session.stopListening();
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 250);
        QCOMPARE(values(microphone), QList<bool>({true, false, true, false}));
        QCOMPARE(preparedAtHolds.first(), false);
        QCOMPARE(heldAtStops, QList<bool>({true, true}));
        QCOMPARE(values(listening), QList<bool>({true, false}));

        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Idle), 1800);
        audio->startResult = false;
        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Error), 250);
        QCOMPARE(values(microphone).mid(4), QList<bool>({true, false}));

        audio->started = false;
        session.startListening();
        session.stopListening();
        QCOMPARE(values(microphone).mid(6), QList<bool>({true, false}));
        QVERIFY(!audio->started);
        QCOMPARE(values(listening), QList<bool>({true, false}));
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
        QTRY_COMPARE(int(session.state()), int(DictationState::Paused));
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
        QVERIFY(pauseAndWait(session));
        speech->emitFailure(QStringLiteral("stream closed"), true, SpeechPhase::Streaming);
        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(message.count(), 1, 250);
        QCOMPARE(message.first().at(0).toString(),
                 QStringLiteral("Used raw transcript • Sent to the app • Part of the dictation may be missing. "
                                "The connection dropped."));
    }

    void dictationSessionNeverCapturesScreenshotTheRefinementDrops_data()
    {
        QTest::addColumn<bool>("secure");
        QTest::newRow("secure target") << true;
        QTest::newRow("selection edit") << false;
    }

    void dictationSessionNeverCapturesScreenshotTheRefinementDrops()
    {
        QFETCH(bool, secure);
        SettingsStore settings;
        settings.raw().clear();
        settings.edit([](AppSettings &draft) {
            draft.refinement.providerId = QStringLiteral("openai");
            draft.refinement.includeScreenshotContext = true;
            return true;
        });

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto targetProvider = std::make_unique<FakeTargetProvider>();
        targetProvider->target.applicationId = QStringLiteral("fixture");
        targetProvider->target.accessible = true;
        targetProvider->target.secure = secure;
        if (!secure) {
            targetProvider->target.selectedText = QStringLiteral("the release is tomorrow");
            targetProvider->target.selectionStart = 0;
            targetProvider->target.selectionEnd = 23;
        }
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
        settings.edit([](AppSettings &draft) { draft.refinement.providerId = QStringLiteral("none"); return true; });

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
        speech->emitFailure(QStringLiteral("temporary disconnect"), true, SpeechPhase::Finalize);

        QCOMPARE(speech->startCalls, 1);
        QCOMPARE(speech->audioChunks, QList<QByteArray>({QByteArrayLiteral("pcm")}));
        QCOMPARE(delivery->calls, 0);
        QCOMPARE(int(session.state()), int(DictationState::Error));
    }

    void dictationSessionBackgroundSpeechPreparationDoesNotBlockStartup()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.edit([](AppSettings &draft) { draft.refinement.providerId = QStringLiteral("none"); return true; });

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        registry.speechProvider(QStringLiteral("claude"));
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);
        speech->backgroundPrepare = true;
        speech->backgroundPrepareDelayMs = 500;
        speech->refreshRequired = true;

        QSignalSpy refreshSpy(&session, &DictationSession::popupOAuthRefreshRequested);
        QElapsedTimer timer;
        timer.start();
        session.startListening();

        // Back before the preparation's sleep ends, as waiting on it would
        // not be.
        QVERIFY2(timer.elapsed() < qint64(speech->backgroundPrepareDelayMs), qPrintable(QString::number(timer.elapsed())));
        QCOMPARE(int(session.state()), int(DictationState::Starting));
        QCOMPARE(refreshSpy.count(), 0);
        QVERIFY(!audio->started);

        QTRY_COMPARE(refreshSpy.count(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 1000);
        QCOMPARE(speech->backgroundPrepareCalls(), 1);
        QCOMPARE(speech->prepareCalls, 1);
        QCOMPARE(speech->startCalls, 1);
        QVERIFY(audio->started);
    }

    void dictationStartupFailureKeepsPopupOpenWithMessage()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.edit([](AppSettings &draft) { draft.refinement.providerId = QStringLiteral("none"); return true; });

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        registry.speechProvider(QStringLiteral("claude"));
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);
        speech->prepareFailure =
            ProviderFailure{ProviderFailureKind::Unavailable, QStringLiteral("Claude login cannot be refreshed")};

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
        settings.edit([](AppSettings &draft) { draft.refinement.providerId = QStringLiteral("openai"); return true; });

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
        refiner->refreshDelayMs = 500;
        refiner->refreshRequired = true;

        QSignalSpy refreshSpy(&session, &DictationSession::popupOAuthRefreshRequested);
        QElapsedTimer timer;
        timer.start();
        session.startListening();

        // Back before the refresh's sleep ends, as waiting on it would
        // not be.
        QVERIFY2(timer.elapsed() < qint64(refiner->refreshDelayMs), qPrintable(QString::number(timer.elapsed())));
        QCOMPARE(int(session.state()), int(DictationState::Starting));
        QCOMPARE(refreshSpy.count(), 0);
        QVERIFY(!audio->started);

        QTRY_COMPARE(refreshSpy.count(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 1000);
        QCOMPARE(refiner->refreshRuns(), 1);
        QCOMPARE(refiner->refreshCalls, 1);
        QCOMPARE(speech->startCalls, 1);
        QVERIFY(audio->started);
    }
};

int runDictationSessionLifecycleTests(int argc, char **argv)
{
    DictationSessionLifecycleTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_dictation_session_lifecycle.moc"
