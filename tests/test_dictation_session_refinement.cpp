#include "common/test_doubles.h"
#include "common/test_http.h"
#include "common/test_auth.h"

using namespace speecher::test;


class DictationSessionRefinementTests : public QObject {
    Q_OBJECT

private slots:
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
        speech->emitFinalText(QStringLiteral("please send my email"));
        session.stopListening();

        QTRY_COMPARE_WITH_TIMEOUT(refiner->refineCalls, 1, 1000);
        refiner->emitFailure(QStringLiteral("refinement failed"));

        QTRY_COMPARE_WITH_TIMEOUT(delivery->calls, 1, 1000);
        QCOMPARE(delivery->lastText, QStringLiteral("please send efox@example.com"));
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
        speech->emitFailure(QStringLiteral("provider failed"));

        QTRY_COMPARE_WITH_TIMEOUT(int(session.state()), int(DictationState::Error), 200);
        QCOMPARE(audio->isActive(), false);
        QCOMPARE(media->resumeCalls, 1);
        QCOMPARE(delivery->calls, 0);
        QCOMPARE(session.lastMessage(), QStringLiteral("provider failed"));
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
