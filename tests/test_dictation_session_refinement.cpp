#include "common/test_prelude.h"
#include "common/test_doubles.h"

#include <QScopeGuard>

using namespace speecher::test;


class DictationSessionRefinementTests : public QObject {
    Q_OBJECT

private slots:
    void speechLossWarningSurvivesRefinement_data()
    {
        QTest::addColumn<bool>("exhaustRecovery");
        QTest::addColumn<bool>("deliverySucceeds");
        QTest::newRow("reconnected") << false << true;
        QTest::newRow("exhausted") << true << true;
        QTest::newRow("delivery failed") << false << false;
    }

    void speechLossWarningSurvivesRefinement()
    {
        QFETCH(bool, exhaustRecovery);
        QFETCH(bool, deliverySucceeds);
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));
        settings.setRefinementStyle(QStringLiteral("light_cleanup"));
        FakeAudioInput audio;
        FakeMediaController media;
        FakeDelivery delivery;
        delivery.result.ok = deliverySucceeds;
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        FakeRefiner *refiner = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        registerFakeRefiner(registry, &refiner);
        DictationSession session(&settings, &audio, &media, &delivery, &registry);
        QSignalSpy status(&session, &DictationSession::statusChanged);
        session.startListening();
        QTRY_COMPARE(session.state(), DictationState::Listening);
        speech->emitPartialText(QStringLiteral("surviving words"));
        speech->emitFailure(QStringLiteral("stream closed"), true, QStringLiteral("streaming"));
        if (exhaustRecovery) {
            speech->emitFailure(QStringLiteral("stream closed"), true, QStringLiteral("streaming"));
            speech->emitFailure(QStringLiteral("stream closed"), true, QStringLiteral("streaming"));
        } else {
            session.stopListening();
        }
        QTRY_COMPARE(refiner->refineCalls, 1);
        refiner->emitCompletedText(QStringLiteral("Surviving words."));
        QCOMPARE(delivery.calls, 1);
        QCOMPARE(session.lastTranscript(), QStringLiteral("Surviving words."));
        QVERIFY(session.lastMessage().contains(QStringLiteral("Part of the dictation may be missing")));
        QVERIFY(status.last().first().toString().contains(QStringLiteral("Part of the dictation may be missing")));
        if (deliverySucceeds) QTRY_COMPARE(session.state(), DictationState::Idle);
        session.startListening();
        QTRY_COMPARE(session.state(), DictationState::Listening);
        QVERIFY(session.lastTranscript().isEmpty());
        speech->emitFinalText(QStringLiteral("a complete new session"));
        session.stopListening();
        QTRY_COMPARE(refiner->refineCalls, 2);
        refiner->emitCompletedText(QStringLiteral("A complete new session."));
        QVERIFY(!session.lastMessage().contains(QStringLiteral("may be missing")));
    }

    void dictationSessionRefinesTranscript()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));
        settings.setRefinementStyle(QStringLiteral("light_cleanup"));
        settings.setCustomVocabulary({QStringLiteral("Qt")});

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        FakeRefiner *refiner = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        registerFakeRefiner(registry, &refiner);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);
        QSignalSpy rawPreviewSpy(&session, &DictationSession::previewDisplayChanged);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("rough text"));
        refiner->autoComplete = true;
        refiner->autoCompleteText = QStringLiteral("Polished text.");
        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(refiner->refineCalls, 1, 1000);
        QCOMPARE(refiner->lastRawTranscript, QStringLiteral("rough text"));
        QCOMPARE(refiner->lastVocabulary, QStringList{QStringLiteral("Qt")});
        QCOMPARE(refiner->lastStyle, QStringLiteral("light_cleanup"));
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);
        QCOMPARE(delivery->lastText, QStringLiteral("Polished text."));
        QCOMPARE(rawPreviewSpy.last().at(0).toString(), QStringLiteral("rough text"));
    }

    void dictationSessionStreamsRefinementPreviewWhileRefining_data()
    {
        QTest::addColumn<bool>("transcriptionPreview");
        QTest::addColumn<bool>("refinementPreview");
        QTest::newRow("both") << true << true;
        QTest::newRow("transcription only") << true << false;
        QTest::newRow("refinement only") << false << true;
        QTest::newRow("neither") << false << false;
    }

    void dictationSessionStreamsRefinementPreviewWhileRefining()
    {
        QFETCH(bool, transcriptionPreview);
        QFETCH(bool, refinementPreview);
        SettingsStore settings;
        settings.raw().clear();
        settings.raw().setValue("ui/transcriptionPreviewEnabled", transcriptionPreview);
        settings.raw().setValue("ui/refinementPreviewEnabled", refinementPreview);
        settings.setRefinementProvider(QStringLiteral("openai"));
        settings.setRefinementStyle(QStringLiteral("light_cleanup"));

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        FakeRefiner *refiner = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        registerFakeRefiner(registry, &refiner);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);
        QSignalSpy rawPreviewSpy(&session, &DictationSession::previewDisplayChanged);
        QSignalSpy transcriptSpy(&session, &DictationSession::previewChanged);
        QSignalSpy refinementPreviewSpy(&session,
                                        &DictationSession::popupRefinementPreviewChanged);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("rough text"));
        QCOMPARE(rawPreviewSpy.last().at(0).toString(),
                 transcriptionPreview ? QStringLiteral("rough text") : QString());
        QCOMPARE(transcriptSpy.last().at(0).toString(), QStringLiteral("rough text"));
        session.stopListening();
        QTRY_COMPARE_WITH_TIMEOUT(refiner->refineCalls, 1, 1000);

        refiner->emitDeltaText(QStringLiteral("Polished "));
        refiner->emitDeltaText(QStringLiteral("text arriving now."));
        QCOMPARE(refinementPreviewSpy.count(), 2);
        QCOMPARE(refinementPreviewSpy.last().at(0).toString(),
                 refinementPreview ? QStringLiteral("Polished text arriving now.") : QString());

        refiner->emitCompletedText(QStringLiteral("Polished text arriving now."));
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);
        QCOMPARE(delivery->lastText, QStringLiteral("Polished text arriving now."));
        refiner->emitDeltaText(QStringLiteral("late"));
        QCOMPARE(refinementPreviewSpy.count(), 2);
    }

    void dictationSessionHidesBindingPlaceholdersInRefinementPreview()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));
        settings.setRefinementStyle(QStringLiteral("light_cleanup"));
        QVERIFY(settings.setBindingRules(
            {{QStringLiteral("my email"), QStringLiteral("efox@example.com")}}));

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        FakeRefiner *refiner = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        registerFakeRefiner(registry, &refiner);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);
        QSignalSpy refinementPreviewSpy(&session,
                                        &DictationSession::popupRefinementPreviewChanged);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("please send my email to Alex"));
        session.stopListening();
        QTRY_COMPARE_WITH_TIMEOUT(refiner->refineCalls, 1, 1000);

        // A placeholder still streaming in stays hidden...
        refiner->emitDeltaText(QStringLiteral("Please send SPEECHER_BIN"));
        QCOMPARE(refinementPreviewSpy.last().at(0).toString(),
                 QStringLiteral("Please send"));
        // ...even once it parses, because more digits could follow...
        refiner->emitDeltaText(QStringLiteral("DING_0"));
        QCOMPARE(refinementPreviewSpy.last().at(0).toString(),
                 QStringLiteral("Please send"));
        // ...and is shown restored once the stream moves past it.
        refiner->emitDeltaText(QStringLiteral(" to Alex."));
        QCOMPARE(refinementPreviewSpy.last().at(0).toString(),
                 QStringLiteral("Please send efox@example.com to Alex."));
    }

    void dictationSessionEditsSelectedTextFromSpokenInstructions()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setSelectionEditReviewEnabled(false);
        settings.setRefinementProvider(QStringLiteral("openai"));
        settings.setCustomVocabulary({QStringLiteral("Speecher")});
        settings.setWritingProfileSettings({
            {WritingProfile::Work, QStringLiteral("strong_polish"), QStringLiteral("formal")},
            {WritingProfile::Email, QStringLiteral("balanced"), QStringLiteral("excited")},
            {WritingProfile::Personal, QStringLiteral("light_cleanup"), QStringLiteral("casual")},
            {WritingProfile::Other, QStringLiteral("balanced"), QStringLiteral("none")},
        });

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto target = std::make_unique<FakeTargetProvider>();
        target->target.applicationId = QStringLiteral("org.mozilla.Thunderbird");
        target->target.category = AppCategory::Email;
        target->target.selectedText = QStringLiteral("the release is tomorrow");
        target->target.selectionStart = 10;
        target->target.selectionEnd = 33;
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        FakeRefiner *refiner = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        registerFakeRefiner(registry, &refiner);
        DictationSession session(&settings,
                                 audio.get(),
                                 media.get(),
                                 target.get(),
                                 delivery.get(),
                                 &registry);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("Make this more confident and add a greeting"));
        refiner->autoComplete = true;
        refiner->autoCompleteText = QStringLiteral("Hello team—the release is tomorrow!");
        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(refiner->refineCalls, 1, 1000);
        QCOMPARE(refiner->lastRawTranscript,
                 QStringLiteral("Make this more confident and add a greeting"));
        QCOMPARE(refiner->lastVocabulary, QStringList{QStringLiteral("Speecher")});
        QVERIFY(refiner->lastContext.editSelection);
        QCOMPARE(refiner->lastContext.target.selectedText,
                 QStringLiteral("the release is tomorrow"));
        QCOMPARE(refiner->lastContext.writingProfile, WritingProfile::Email);
        QCOMPARE(refiner->lastContext.tone, QStringLiteral("excited"));
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);
        QCOMPARE(delivery->lastText,
                 QStringLiteral("Hello team—the release is tomorrow!"));
        QCOMPARE(delivery->lastTarget.selectionStart, 10);
        QCOMPARE(delivery->lastTarget.selectionEnd, 33);
    }

    void selectionEditWaitsForReplace()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));
        FakeAudioInput audio;
        FakeMediaController media;
        FakeTargetProvider target;
        target.target.applicationId = QStringLiteral("org.kde.kate");
        target.target.selectedText = QStringLiteral("Move the standup to Wednesday.");
        target.target.selectionStart = 4;
        target.target.selectionEnd = 34;
        FakeDelivery delivery;
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        FakeRefiner *refiner = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        registerFakeRefiner(registry, &refiner);
        DictationSession session(&settings, &audio, &media, &target, &delivery, &registry);
        session.setReviewKeys({QStringLiteral("Esc"), QStringLiteral("Enter")});
        QSignalSpy reviews(&session, &DictationSession::popupSelectionEditReviewRequested);

        session.startListening();
        QTRY_COMPARE(session.state(), DictationState::Listening);
        speech->emitFinalText(QStringLiteral("make it Thursday"));
        session.stopListening();
        QTRY_COMPARE(refiner->refineCalls, 1);
        refiner->emitCompletedText(QStringLiteral("Move the standup to Thursday."));

        QCOMPARE(session.state(), DictationState::Reviewing);
        QCOMPARE(delivery.calls, 0);
        QCOMPARE(reviews.count(), 1);
        const auto review = reviews.first().first().value<SelectionEditReview>();
        QCOMPARE(review.instruction, QStringLiteral("“make it Thursday”"));
        QCOMPARE(review.summary, QStringLiteral("1 change"));
        QCOMPARE(review.keys.replace, QStringLiteral("Enter"));
        // Keys the controller lets go of leave the buttons at once.
        session.setReviewKeys({QStringLiteral("Esc"), QString()});
        QCOMPARE(reviews.count(), 2);
        QVERIFY(reviews.last().first().value<SelectionEditReview>().keys.replace.isEmpty());

        session.replaceSelection();
        QCOMPARE(delivery.calls, 1);
        QCOMPARE(delivery.lastText, QStringLiteral("Move the standup to Thursday."));
        QCOMPARE(delivery.lastTarget.selectionStart, 4);
        QCOMPARE(delivery.lastSettings.pasteRules, defaultPasteRules());
        QCOMPARE(session.lastMessage(), QStringLiteral("Input sent"));
    }

    // A follow-up is dictated as a session of its own, edits the revision
    // under review rather than the selection, and comes back as a review
    // against the original selection.
    void followUpEditsTheRevisionUnderReview()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));
        settings.setInsightsEnabled(true);
        FakeAudioInput audio;
        FakeMediaController media;
        FakeTargetProvider target;
        target.target.applicationId = QStringLiteral("org.kde.kate");
        target.target.selectedText = QStringLiteral("Move the standup to Wednesday.");
        target.target.selectionStart = 4;
        target.target.selectionEnd = 34;
        FakeDelivery delivery;
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        FakeRefiner *refiner = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        registerFakeRefiner(registry, &refiner);
        DictationSession session(&settings, &audio, &media, &target, &delivery, &registry);
        QSignalSpy reviews(&session, &DictationSession::popupSelectionEditReviewRequested);
        QSignalSpy ended(&session, &DictationSession::popupSelectionEditReviewEnded);
        QSignalSpy recorded(&session, &DictationSession::dictationRecorded);
        const auto lastReview = [&reviews] { return reviews.last().first().value<SelectionEditReview>(); };

        session.startListening();
        QTRY_COMPARE(session.state(), DictationState::Listening);
        speech->emitFinalText(QStringLiteral("make it Thursday"));
        session.stopListening();
        QTRY_COMPARE(refiner->refineCalls, 1);
        refiner->emitCompletedText(QStringLiteral("Move the standup to Thursday."));
        QCOMPARE(session.state(), DictationState::Reviewing);
        const int capturesBefore = target.captureCalls;

        session.toggle();
        QVERIFY(lastReview().following);
        QTRY_COMPARE(session.state(), DictationState::Listening);
        speech->emitFinalText(QStringLiteral("actually make it Friday"));
        session.stopListening();
        QTRY_COMPARE(refiner->refineCalls, 2);
        // The follow-up edits the revision, and the Target is not read again.
        QCOMPARE(refiner->lastContext.target.selectedText, QStringLiteral("Move the standup to Thursday."));
        QCOMPARE(refiner->lastRawTranscript, QStringLiteral("actually make it Friday"));
        QCOMPARE(target.captureCalls, capturesBefore);
        refiner->emitCompletedText(QStringLiteral("Move the standup to Friday."));

        QCOMPARE(session.state(), DictationState::Reviewing);
        const SelectionEditReview review = lastReview();
        QVERIFY(!review.following);
        QCOMPARE(review.instruction, QStringLiteral("“make it Thursday” then “actually make it Friday”"));
        // Against the original selection: Wednesday became Friday.
        QCOMPARE(review.summary, QStringLiteral("1 change"));
        QCOMPARE(ended.count(), 0);
        QCOMPARE(delivery.calls, 0);

        session.replaceSelection();
        QCOMPARE(ended.count(), 1);
        QCOMPARE(delivery.calls, 1);
        QCOMPARE(delivery.lastText, QStringLiteral("Move the standup to Friday."));
        QCOMPARE(delivery.lastTarget.selectionStart, 4);
        // Insights counts every instruction spoken for the edit.
        QCOMPARE(recorded.count(), 1);
        QCOMPARE(recorded.first().first().value<DictationRecord>().words, 7);
        QCOMPARE(recorded.first().first().value<DictationRecord>().refinementProviders,
                 QStringList{QStringLiteral("openai")});
    }

    void aFollowUpThatComesToNothingLeavesTheReview_data()
    {
        QTest::addColumn<QString>("how");
        QTest::newRow("cancelled while listening") << QStringLiteral("cancel");
        // Push-to-talk starts rather than toggles.
        QTest::newRow("started by push-to-talk, then cancelled") << QStringLiteral("push-to-talk");
        QTest::newRow("refinement failed") << QStringLiteral("refinement");
        QTest::newRow("nothing heard") << QStringLiteral("silence");
        QTest::newRow("stopped while refining") << QStringLiteral("stop refining");
    }

    void aFollowUpThatComesToNothingLeavesTheReview()
    {
        QFETCH(QString, how);
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));
        FakeAudioInput audio;
        FakeMediaController media;
        FakeTargetProvider target;
        target.target.applicationId = QStringLiteral("org.kde.kate");
        target.target.selectedText = QStringLiteral("Move the standup to Wednesday.");
        target.target.selectionStart = 0;
        target.target.selectionEnd = 30;
        FakeDelivery delivery;
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        FakeRefiner *refiner = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        registerFakeRefiner(registry, &refiner);
        DictationSession session(&settings, &audio, &media, &target, &delivery, &registry);
        QSignalSpy reviews(&session, &DictationSession::popupSelectionEditReviewRequested);
        QSignalSpy ended(&session, &DictationSession::popupSelectionEditReviewEnded);
        QSignalSpy errors(&session, &DictationSession::popupErrorRequested);

        session.startListening();
        QTRY_COMPARE(session.state(), DictationState::Listening);
        speech->emitFinalText(QStringLiteral("make it Thursday"));
        session.stopListening();
        QTRY_COMPARE(refiner->refineCalls, 1);
        refiner->emitCompletedText(QStringLiteral("Move the standup to Thursday."));
        const SelectionEditReview before = reviews.last().first().value<SelectionEditReview>();

        if (how == QStringLiteral("push-to-talk")) {
            session.startListening();
        } else {
            session.toggle();
        }
        QTRY_COMPARE(session.state(), DictationState::Listening);
        if (how == QStringLiteral("cancel") || how == QStringLiteral("push-to-talk")) {
            session.cancel();
        } else if (how == QStringLiteral("stop refining")) {
            speech->emitFinalText(QStringLiteral("actually make it Friday"));
            session.stopListening();
            QTRY_COMPARE(refiner->refineCalls, 2);
            session.toggle();
        } else if (how == QStringLiteral("refinement")) {
            speech->emitFinalText(QStringLiteral("actually make it Friday"));
            session.stopListening();
            QTRY_COMPARE(refiner->refineCalls, 2);
            refiner->emitFailure(QStringLiteral("refinement unavailable"));
        } else {
            session.stopListening();
        }

        QTRY_COMPARE(session.state(), DictationState::Reviewing);
        const SelectionEditReview after = reviews.last().first().value<SelectionEditReview>();
        QVERIFY(!after.following);
        QCOMPARE(after.instruction, before.instruction);
        QCOMPARE(after.runs, before.runs);
        QCOMPARE(ended.count(), 0);
        QCOMPARE(errors.count(), 0);
        session.replaceSelection();
        QCOMPARE(delivery.lastText, QStringLiteral("Move the standup to Thursday."));
    }

    void aFollowUpAskedForWhileTheMicrophoneStopsWaits_data()
    {
        QTest::addColumn<bool>("cancelIt");
        QTest::newRow("then stopped") << false;
        QTest::newRow("then cancelled") << true;
    }

    // A follow-up asked for while a cancelled one still stops the microphone
    // waits for it. Dropped before it begins, by a stop or a cancel, it leaves
    // the review shown as it was, buttons and all.
    void aFollowUpAskedForWhileTheMicrophoneStopsWaits()
    {
        QFETCH(bool, cancelIt);
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));
        FakeAudioInput audio;
        FakeMediaController media;
        FakeTargetProvider target;
        target.target.applicationId = QStringLiteral("org.kde.kate");
        target.target.selectedText = QStringLiteral("Move the standup to Wednesday.");
        target.target.selectionStart = 0;
        target.target.selectionEnd = 30;
        FakeDelivery delivery;
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        FakeRefiner *refiner = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        registerFakeRefiner(registry, &refiner);
        DictationSession session(&settings, &audio, &media, &target, &delivery, &registry);
        QSignalSpy reviews(&session, &DictationSession::popupSelectionEditReviewRequested);
        session.startListening();
        QTRY_COMPARE(session.state(), DictationState::Listening);
        speech->emitFinalText(QStringLiteral("make it Thursday"));
        session.stopListening();
        QTRY_COMPARE(refiner->refineCalls, 1);
        refiner->emitCompletedText(QStringLiteral("Move the standup to Thursday."));

        session.toggle();
        QTRY_COMPARE(session.state(), DictationState::Listening);
        bool asked = false;
        audio.onStop = [&] {
            if (std::exchange(asked, true)) {
                return;
            }
            session.toggle();
            QVERIFY(session.startPending());
            QVERIFY(!reviews.last().first().value<SelectionEditReview>().following);
            if (cancelIt) {
                session.cancel();
            } else {
                session.stopListening();
            }
        };
        session.cancel();
        QCoreApplication::processEvents();
        QVERIFY(!session.startPending());
        QCOMPARE(session.state(), DictationState::Reviewing);
        QVERIFY(!reviews.last().first().value<SelectionEditReview>().following);
    }

    // What a failed follow-up met is not the revision kept: its receipt is
    // the one that revision would have had.
    void aFailedFollowUpLeavesTheKeptRevisionsReceipt()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));
        FakeAudioInput audio;
        FakeMediaController media;
        FakeTargetProvider target;
        target.target.applicationId = QStringLiteral("org.kde.kate");
        target.target.selectedText = QStringLiteral("Move the standup to Wednesday.");
        target.target.selectionStart = 0;
        target.target.selectionEnd = 30;
        FakeDelivery delivery;
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        FakeRefiner *refiner = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        registerFakeRefiner(registry, &refiner);
        DictationSession session(&settings, &audio, &media, &target, &delivery, &registry);
        session.startListening();
        QTRY_COMPARE(session.state(), DictationState::Listening);
        speech->emitFinalText(QStringLiteral("make it Thursday"));
        session.stopListening();
        QTRY_COMPARE(refiner->refineCalls, 1);
        refiner->emitCompletedText(QStringLiteral("Move the standup to Thursday."));

        session.toggle();
        QTRY_COMPARE(session.state(), DictationState::Listening);
        speech->emitPartialText(QStringLiteral("actually Friday"));
        speech->emitFailure(QStringLiteral("connection lost"));
        QTRY_COMPARE(refiner->refineCalls, 2);
        refiner->emitFailure(QStringLiteral("refinement unavailable"));
        QCOMPARE(session.state(), DictationState::Reviewing);
        session.replaceSelection();
        QCOMPARE(delivery.lastText, QStringLiteral("Move the standup to Thursday."));
        QCOMPARE(session.lastMessage(), QStringLiteral("Input sent"));
    }

    void selectionEditReviewEndsWithoutReplacing_data()
    {
        QTest::addColumn<bool>("selectionMoved");
        QTest::addColumn<bool>("focusMoved");
        QTest::newRow("keep original") << false << false;
        QTest::newRow("selection changed, then replace") << true << false;
        // Another control with the same selection is still not the one the
        // edit was made in.
        QTest::newRow("another control focused, then replace") << false << true;
    }

    // Keep original leaves the selection alone and says nothing. Replace after
    // the selection moved must not paste over the new one: the edit goes to
    // the clipboard and the receipt says why.
    void selectionEditReviewEndsWithoutReplacing()
    {
        QFETCH(bool, selectionMoved);
        QFETCH(bool, focusMoved);
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));
        FakeAudioInput audio;
        FakeMediaController media;
        FakeTargetProvider target;
        target.target.applicationId = QStringLiteral("org.kde.kate");
        target.target.selectedText = QStringLiteral("Keep this original text");
        target.target.selectionStart = 0;
        target.target.selectionEnd = 23;
        FakeDelivery delivery;
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        FakeRefiner *refiner = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        registerFakeRefiner(registry, &refiner);
        DictationSession session(&settings, &audio, &media, &target, &delivery, &registry);
        QSignalSpy hidden(&session, &DictationSession::popupHideRequested);
        QSignalSpy messages(&session, &DictationSession::popupMessageRequested);
        QSignalSpy ended(&session, &DictationSession::popupSelectionEditReviewEnded);

        session.startListening();
        QTRY_COMPARE(session.state(), DictationState::Listening);
        speech->emitFinalText(QStringLiteral("make this shorter"));
        session.stopListening();
        QTRY_COMPARE(refiner->refineCalls, 1);
        refiner->emitCompletedText(QStringLiteral("Keep the original"));
        QCOMPARE(session.state(), DictationState::Reviewing);

        if (!selectionMoved && !focusMoved) {
            session.cancel();
            QCOMPARE(session.state(), DictationState::Idle);
            QCOMPARE(delivery.calls, 0);
            QCOMPARE(hidden.count(), 1);
            QCOMPARE(messages.count(), 0);
            QCOMPARE(ended.count(), 1);
            return;
        }
        if (selectionMoved) {
            target.target.selectedText.clear();
            target.target.selectionStart = target.target.selectionEnd = 7;
        }
        target.focused = !focusMoved;
        session.replaceSelection();
        QCOMPARE(delivery.calls, 1);
        QCOMPARE(delivery.lastText, QStringLiteral("Keep the original"));
        QCOMPARE(delivery.lastSettings.pasteRules,
                 (QList<PasteRule>{{PasteRuleScope::Global, QString(), PasteMethod::ClipboardOnly}}));
        QCOMPARE(session.lastMessage(), QStringLiteral("Input sent • Your selection changed"));
    }

    // Speech that dropped after the instruction was heard leaves the session
    // ready to paste the raw words, but a refined edit replaced is no fallback.
    void aReviewedEditAfterSpeechDroppedIsNotTheRawTranscript()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));
        FakeAudioInput audio;
        FakeMediaController media;
        FakeTargetProvider target;
        target.target.applicationId = QStringLiteral("org.kde.kate");
        target.target.selectedText = QStringLiteral("the release is tomorrow");
        target.target.selectionStart = 0;
        target.target.selectionEnd = 23;
        FakeDelivery delivery;
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        FakeRefiner *refiner = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        registerFakeRefiner(registry, &refiner);
        DictationSession session(&settings, &audio, &media, &target, &delivery, &registry);

        session.startListening();
        QTRY_COMPARE(session.state(), DictationState::Listening);
        speech->emitPartialText(QStringLiteral("make it friday"));
        for (int failure = 0; failure < 3; ++failure) {
            speech->emitFailure(QStringLiteral("stream closed"), true, QStringLiteral("streaming"));
        }
        QTRY_COMPARE(refiner->refineCalls, 1);
        refiner->emitCompletedText(QStringLiteral("The release is Friday."));
        QCOMPARE(session.state(), DictationState::Reviewing);
        session.replaceSelection();
        QCOMPARE(delivery.calls, 1);
        QVERIFY(!session.lastMessage().contains(QStringLiteral("Used raw transcript")));
    }

    void selectionEditingFailurePreservesTheSelectedText()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto target = std::make_unique<FakeTargetProvider>();
        target->target.applicationId = QStringLiteral("org.kde.kate");
        target->target.selectedText = QStringLiteral("Keep this original text");
        target->target.selectionStart = 0;
        target->target.selectionEnd = 23;
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        FakeRefiner *refiner = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        registerFakeRefiner(registry, &refiner);
        DictationSession session(&settings,
                                 audio.get(),
                                 media.get(),
                                 target.get(),
                                 delivery.get(),
                                 &registry);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("Make this shorter"));
        session.stopListening();
        QTRY_COMPARE_WITH_TIMEOUT(refiner->refineCalls, 1, 1000);
        refiner->emitFailure(QStringLiteral("refinement unavailable"));

        QCOMPARE(delivery->calls, 0);
        QCOMPARE(int(session.state()), int(DictationState::Error));
        QCOMPARE(session.lastMessage(), QStringLiteral("refinement unavailable"));
    }

    void dictationSessionAppliesDetectedProfileSettingsAndOverrides()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));
        settings.setWritingProfileSettings({
            {WritingProfile::Work, QStringLiteral("strong_polish"), QStringLiteral("formal")},
            {WritingProfile::Email, QStringLiteral("balanced"), QStringLiteral("none")},
            {WritingProfile::Personal, QStringLiteral("light_cleanup"), QStringLiteral("casual")},
            {WritingProfile::Other, QStringLiteral("none"), QStringLiteral("none")},
        });
        settings.setWritingProfileOverrides({
            {QStringLiteral("firefox"), WritingProfile::Work, true},
        });

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto target = std::make_unique<FakeTargetProvider>();
        target->target.applicationId = QStringLiteral("firefox");
        target->target.category = AppCategory::Browser;
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        FakeRefiner *refiner = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        registerFakeRefiner(registry, &refiner);
        DictationSession session(&settings,
                                 audio.get(),
                                 media.get(),
                                 target.get(),
                                 delivery.get(),
                                 &registry);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("profile text"));
        refiner->autoComplete = true;
        refiner->autoCompleteText = QStringLiteral("Profile text.");
        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(refiner->refineCalls, 1, 1000);
        QCOMPARE(refiner->lastStyle, QStringLiteral("strong_polish"));
        QCOMPARE(refiner->lastTone, QStringLiteral("formal"));
        QCOMPARE(refiner->lastContext.writingProfile, WritingProfile::Work);
        QCOMPARE(refiner->lastContext.tone, QStringLiteral("formal"));
    }

    void dictationSessionAppliesBindingsWhenRefinementDisabled()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("none"));
        QVERIFY(settings.setBindingRules({{QStringLiteral("my email"), QStringLiteral("efox@example.com")}}));

        auto audio = std::make_unique<FakeAudioInput>();
        auto media = std::make_unique<FakeMediaController>();
        auto delivery = std::make_unique<FakeDelivery>();
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("My, email!"));
        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);
        QCOMPARE(delivery->lastText, QStringLiteral("efox@example.com!"));
    }

    void dictationSessionSkipsRefinementWhenBindingsCoverTranscript()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));
        QVERIFY(settings.setBindingRules({
            {QStringLiteral("my email"), QStringLiteral("efox@example.com")},
            {QStringLiteral("my phone"), QStringLiteral("+1 555 0100")},
        }));

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
        speech->emitFinalText(QStringLiteral("my email, my phone"));
        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);
        QCOMPARE(delivery->lastText, QStringLiteral("efox@example.com, +1 555 0100"));
        QCOMPARE(refiner->prepareCalls, 0);
        QCOMPARE(refiner->refineCalls, 0);
    }

    void dictationSessionProtectsBindingsDuringRefinement()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));
        settings.setCustomVocabulary({QStringLiteral("Qt")});
        QVERIFY(settings.setBindingRules({{QStringLiteral("my email"), QStringLiteral("efox@example.com")}}));

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
        speech->emitFinalText(QStringLiteral("please send my email to Alex"));
        refiner->autoComplete = true;
        refiner->autoCompleteText = QStringLiteral("Please send SPEECHER_BINDING_0 to Alex.");
        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(refiner->refineCalls, 1, 1000);
        QCOMPARE(refiner->lastRawTranscript, QStringLiteral("please send SPEECHER_BINDING_0 to Alex"));
        QCOMPARE(refiner->lastVocabulary, QStringList({QStringLiteral("Qt")}));
        QCOMPARE(refiner->lastBindingVocabulary, QStringList({QStringLiteral("my email")}));
        QVERIFY(!refiner->lastVocabulary.contains(QStringLiteral("efox@example.com")));
        QVERIFY(!refiner->lastBindingVocabulary.contains(QStringLiteral("efox@example.com")));
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);
        QCOMPARE(delivery->lastText, QStringLiteral("Please send efox@example.com to Alex."));
    }

    void dictationSessionAppliesBindingsCorrectedByRefinement()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));
        settings.setCustomVocabulary({QStringLiteral("Qt")});
        QVERIFY(settings.setBindingRules({{QStringLiteral("my email"), QStringLiteral("efox@example.com")}}));

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
        speech->emitFinalText(QStringLiteral("please send my evil to Alex"));
        refiner->autoComplete = true;
        refiner->autoCompleteText = QStringLiteral("Please send my email to Alex.");
        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(refiner->refineCalls, 1, 1000);
        QCOMPARE(refiner->lastRawTranscript, QStringLiteral("please send my evil to Alex"));
        QCOMPARE(refiner->lastVocabulary, QStringList({QStringLiteral("Qt")}));
        QCOMPARE(refiner->lastBindingVocabulary, QStringList({QStringLiteral("my email")}));
        QVERIFY(!refiner->lastVocabulary.contains(QStringLiteral("efox@example.com")));
        QVERIFY(!refiner->lastBindingVocabulary.contains(QStringLiteral("efox@example.com")));
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);
        QCOMPARE(delivery->lastText, QStringLiteral("Please send efox@example.com to Alex."));
    }

    void dictationSessionPostRefinementBindingsPreserveExistingPlaceholders()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));
        QVERIFY(settings.setBindingRules({
            {QStringLiteral("my email"), QStringLiteral("efox@example.com")},
            {QStringLiteral("speecher binding"), QStringLiteral("bad replacement")},
        }));

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
        speech->emitFinalText(QStringLiteral("please send my email and my evil"));
        refiner->autoComplete = true;
        refiner->autoCompleteText = QStringLiteral("Please send SPEECHER_BINDING_0 and my email.");
        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(refiner->refineCalls, 1, 1000);
        QCOMPARE(refiner->lastRawTranscript, QStringLiteral("please send SPEECHER_BINDING_0 and my evil"));
        QCOMPARE(refiner->lastVocabulary, QStringList());
        QCOMPARE(refiner->lastBindingVocabulary,
                 QStringList({QStringLiteral("my email"), QStringLiteral("speecher binding")}));
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);
        QCOMPARE(delivery->lastText, QStringLiteral("Please send efox@example.com and efox@example.com."));
    }

    void dictationSessionHonorsDoNotBindRequest()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));
        QVERIFY(settings.setBindingRules({{QStringLiteral("my email"), QStringLiteral("efox@example.com")}}));

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
        speech->emitFinalText(QStringLiteral("please write my email but don't turn that into a binding"));
        refiner->autoComplete = true;
        refiner->autoCompleteText = QStringLiteral("Please write my email.");
        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(refiner->refineCalls, 1, 1000);
        QCOMPARE(refiner->lastRawTranscript,
                 QStringLiteral("please write my email but don't turn that into a binding"));
        QCOMPARE(refiner->lastBindingVocabulary, QStringList({QStringLiteral("my email")}));
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);
        QCOMPARE(delivery->lastText, QStringLiteral("Please write my email."));
    }

    void dictationSessionDoesNotPostBindAmbiguousDoNotBindRequest()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));
        QVERIFY(settings.setBindingRules({{QStringLiteral("my email"), QStringLiteral("efox@example.com")}}));

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
        speech->emitFinalText(QStringLiteral("please write my evil but don't turn that into a binding"));
        refiner->autoComplete = true;
        refiner->autoCompleteText = QStringLiteral("Please write my email.");
        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(refiner->refineCalls, 1, 1000);
        QCOMPARE(refiner->lastRawTranscript,
                 QStringLiteral("please write my evil but don't turn that into a binding"));
        QCOMPARE(refiner->lastBindingVocabulary, QStringList({QStringLiteral("my email")}));
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);
        QCOMPARE(delivery->lastText, QStringLiteral("Please write my email."));
    }

    void dictationSessionRefinerFailureFallsBackToBoundText()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));
        QVERIFY(settings.setBindingRules({{QStringLiteral("my email"), QStringLiteral("efox@example.com")}}));

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
        speech->emitFinalText(QStringLiteral("please send my email"));
        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(refiner->refineCalls, 1, 1000);
        refiner->emitFailure(QStringLiteral("refinement failed"));

        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);
        QCOMPARE(delivery->lastText, QStringLiteral("please send efox@example.com"));
    }

    void dictationSessionCanCancelRefinementWithRawFallback()
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
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("raw fallback"));
        session.stopListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Refining), 250);

        session.stopListening();

        QCOMPARE(refiner->cancelCalls, 1);
        QCOMPARE(delivery->calls, 1);
        QCOMPARE(delivery->lastText, QStringLiteral("raw fallback"));
    }

    void dictationSessionCorruptedPlaceholderFallsBackToBoundText()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));
        QVERIFY(settings.setBindingRules({{QStringLiteral("my email"), QStringLiteral("efox@example.com")}}));

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
        speech->emitFinalText(QStringLiteral("please send my email"));
        refiner->autoComplete = true;
        refiner->autoCompleteText = QStringLiteral("Please send SPEECHER_BINDING_ZERO.");
        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);
        QCOMPARE(delivery->lastText, QStringLiteral("please send efox@example.com"));
    }

    void dictationSessionFallsBackWhenRefinementUnavailable()
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
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("raw fallback"));
        refiner->prepareResult = {false, QStringLiteral("No OpenAI credential found")};
        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);
        QCOMPARE(refiner->prepareCalls, 1);
        QCOMPARE(refiner->refineCalls, 0);
        QCOMPARE(delivery->lastText, QStringLiteral("raw fallback"));
    }

    void dictationSessionStopsOnEmptySpeechFailure()
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
        speech->emitFailure(QStringLiteral("provider failed"));

        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Error), 200);
        QCOMPARE(audio->isActive(), false);
        QCOMPARE(media->resumeCalls, 1);
        QCOMPARE(delivery->calls, 0);
        QCOMPARE(session.lastMessage(), QStringLiteral("provider failed"));
    }

    void dictationSessionDeliversTranscriptAfterSpeechFailureWhileListening()
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
        speech->emitFinalText(QStringLiteral("keep this transcript"));
        QSignalSpy message(&session, &DictationSession::popupMessageRequested);
        // Non-retryable: the session must not try to reconnect, only deliver.
        speech->emitFailure(QStringLiteral("provider disconnected"), false);

        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 250);
        QCOMPARE(delivery->lastText, QStringLiteral("keep this transcript"));
        QVERIFY(session.lastMessage().contains(QStringLiteral("Part of the dictation may be missing")));
        QCOMPARE(audio->isActive(), false);
        QCOMPARE(media->resumeCalls, 1);
        QCOMPARE(message.count(), 1);
        QCOMPARE(message.first().first().toString(),
                 QStringLiteral("Used raw transcript • Input sent • Part of the dictation may be missing. The connection dropped."));
        QCOMPARE(message.first().at(1).value<PopupOutcome>(), PopupOutcome::Fallback);
    }

    void dictationSessionReconnectsSpeechStreamAfterRetryableFailureWhileListening()
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
        speech->emitFinalText(QStringLiteral("keep this"));
        speech->emitPartialText(QStringLiteral("and these words"));
        QSignalSpy status(&session, &DictationSession::popupStatusChanged);
        QSignalSpy frozen(&session, &DictationSession::popupFrozenChanged);
        speech->emitFailure(QStringLiteral("stream closed"), true, QStringLiteral("streaming"));

        QCOMPARE(int(session.state()), int(DictationState::Listening));
        QCOMPARE(audio->isActive(), true);
        QCOMPARE(delivery->calls, 0);
        QCOMPARE(speech->startCalls, 2);
        QCOMPARE(media->resumeCalls, 0);
        QCOMPARE(status.count(), 0);
        QCOMPARE(frozen.count(), 0);
        // The transcriber replaces its own client inside startAttempt; no separate cancel.
        QVERIFY(speech->cancelledAttempts.isEmpty());

        audio->pushAudio(QByteArrayLiteral("pcm"));
        QCOMPARE(speech->audioChunks, QList<QByteArray>({QByteArrayLiteral("pcm")}));

        speech->emitFinalText(QStringLiteral("after reconnect"));
        session.stopListening();
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 250);
        QCOMPARE(delivery->lastText, QStringLiteral("keep this and these words after reconnect"));
        QVERIFY(session.lastMessage().contains(QStringLiteral("Part of the dictation may be missing")));
    }

    void dictationSessionDeliversTranscriptWhenSpeechReconnectsAreExhausted()
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
        speech->emitFinalText(QStringLiteral("keep this transcript"));
        speech->emitFailure(QStringLiteral("stream closed"), true, QStringLiteral("streaming"));
        speech->emitFailure(QStringLiteral("stream closed"), true, QStringLiteral("streaming"));
        QCOMPARE(speech->startCalls, 3);
        QCOMPARE(int(session.state()), int(DictationState::Listening));
        QCOMPARE(delivery->calls, 0);

        speech->emitFailure(QStringLiteral("stream closed"), true, QStringLiteral("streaming"));

        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 250);
        QCOMPARE(delivery->lastText, QStringLiteral("keep this transcript"));
        QVERIFY(session.lastMessage().contains(QStringLiteral("Part of the dictation may be missing")));
        QCOMPARE(speech->startCalls, 3);
        QCOMPARE(audio->isActive(), false);
        QCOMPARE(media->resumeCalls, 1);

        // The reconnect budget belongs to the session: a new one starts fresh.
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Idle), 2000);
        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFailure(QStringLiteral("stream closed"), true, QStringLiteral("streaming"));
        QCOMPARE(speech->startCalls, 5);
        QCOMPARE(int(session.state()), int(DictationState::Listening));
    }

    void dictationSessionTreatsAStreamEndedAtOnceAsAFailure()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("none"));
        FakeAudioInput audio;
        FakeMediaController media;
        FakeDelivery delivery;
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, &audio, &media, &delivery, &registry);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(session.state(), DictationState::Listening, 250);
        speech->emitFinalText(QStringLiteral("keep this"));
        // A provider that ends every stream as soon as it opens must not make
        // the session reopen streams without bound.
        speech->emitCompletion();
        speech->emitCompletion();
        QCOMPARE(speech->startCalls, 3);
        QCOMPARE(session.state(), DictationState::Listening);
        speech->emitCompletion();

        QTRY_COMPARE_WITH_TIMEOUT(delivery.calls, 1, 250);
        QCOMPARE(delivery.lastText, QStringLiteral("keep this"));
        QCOMPARE(speech->startCalls, 3);
        QVERIFY(session.lastMessage().contains(QStringLiteral("Part of the dictation may be missing")));
    }

    void dictationSessionRestoresReconnectBudgetAfterAStableStream()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("none"));
        FakeAudioInput audio;
        FakeMediaController media;
        FakeDelivery delivery;
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        DictationSession session(&settings, &audio, &media, &delivery, &registry);
        const int stableAttemptMs = DictationSession::stableAttemptMs();
        DictationSession::setStableAttemptMs(0);
        const auto restore = qScopeGuard([stableAttemptMs] {
            DictationSession::setStableAttemptMs(stableAttemptMs);
        });

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(session.state(), DictationState::Listening, 250);
        // Each drop follows a stream that had been healthy, so none of them
        // spends the budget meant for a stream that keeps failing.
        for (int drop = 0; drop < 5; ++drop) {
            speech->emitFailure(QStringLiteral("stream closed"), true, QStringLiteral("streaming"));
        }
        QCOMPARE(speech->startCalls, 6);
        QCOMPARE(session.state(), DictationState::Listening);
    }

    void dictationSessionDoesNotReconnectAfterConnectPhaseFailure()
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
        // Retryable, but the stream never came up: the user should see the error now.
        speech->emitFailure(QStringLiteral("connection refused"), true, QStringLiteral("connect"));

        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Error), 200);
        QCOMPARE(speech->startCalls, 1);
    }

    void dictationSessionFreezesTranscriptAfterSpeechFailureWhileRefining()
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
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("keep this transcript"));
        QSignalSpy frozen(&session, &DictationSession::popupFrozenChanged);
        QSignalSpy message(&session, &DictationSession::popupMessageRequested);
        speech->emitFailure(QStringLiteral("provider disconnected"));

        QTRY_COMPARE_WITH_TIMEOUT(refiner->refineCalls, 1, 250);
        QCOMPARE(refiner->lastRawTranscript, QStringLiteral("keep this transcript"));
        QCOMPARE(speech->cancelledAttempts,
                 QList<quint64>({speech->currentAttemptId}));
        QCOMPARE(frozen.count(), 1);
        QCOMPARE(frozen.first().first().toBool(), true);

        QSignalSpy preview(&session, &DictationSession::previewChanged);
        speech->emitFinalText(QStringLiteral("late buffered final"));
        QCOMPARE(preview.count(), 0);

        refiner->emitCompletedText(QStringLiteral("Keep this transcript."));
        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 250);
        QCOMPARE(delivery->lastText, QStringLiteral("Keep this transcript."));
        QCOMPARE(message.count(), 1);
        QCOMPARE(message.first().first().toString(), QStringLiteral("Input sent • Part of the dictation may be missing. The connection dropped."));
    }

    void dictationSessionIgnoresRefinerSignalsAfterFailureFallback()
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
        DictationSession session(&settings, audio.get(), media.get(), delivery.get(), &registry);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Listening), 250);
        speech->emitFinalText(QStringLiteral("raw transcript"));
        session.stopListening();
        QTRY_COMPARE_WITH_TIMEOUT(refiner->refineCalls, 1, 250);

        refiner->emitFailure(QStringLiteral("refinement failed"));
        refiner->emitCompletedText(QStringLiteral("late completion"));

        QCOMPARE(delivery->calls, 1);
        QCOMPARE(delivery->lastText, QStringLiteral("raw transcript"));
    }

    void dictationSessionStopsOnEmptyAudioFailure()
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
        audio->emitFailure(QStringLiteral("microphone blocked"));

        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Error), 200);
        QCOMPARE(audio->isActive(), false);
        QCOMPARE(speech->stopCalls, 0);
        QCOMPARE(speech->cancelledAttempts, QList<quint64>({speech->currentAttemptId}));
        QCOMPARE(media->resumeCalls, 1);
        QCOMPARE(delivery->calls, 0);
        QCOMPARE(session.lastMessage(), QStringLiteral("microphone blocked"));
    }

    void dictationStopClearsAnErrorAndRemainsIdempotent()
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
        audio->emitFailure(QStringLiteral("microphone blocked"));
        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Error), 200);

        session.stopListening();
        QCOMPARE(int(session.state()), int(DictationState::Idle));
        session.stopListening();
        QCOMPARE(int(session.state()), int(DictationState::Idle));
    }
};

int runDictationSessionRefinementTests(int argc, char **argv)
{
    DictationSessionRefinementTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_dictation_session_refinement.moc"
