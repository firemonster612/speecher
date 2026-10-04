#include "common/test_prelude.h"
#include "common/test_doubles.h"
#include "core/settings/SettingsKeys.h"

#include <QScopeGuard>

#include <map>

using namespace speecher::test;

namespace {

using Stage = ProviderAttemptIssue::Stage;

QString testLabel(ProviderRole role, const QString &id)
{
    static const QHash<QString, QString> speech{{QStringLiteral("claude"), QStringLiteral("Claude Voice")},
                                                {QStringLiteral("codex"), QStringLiteral("ChatGPT Codex")},
                                                {QStringLiteral("local"), QStringLiteral("Local Model")},
                                                {QStringLiteral("endpoint"), QStringLiteral("Custom Endpoint")}};
    static const QHash<QString, QString> refinement{{QStringLiteral("openai"), QStringLiteral("OpenAI")},
                                                    {QStringLiteral("local"), QStringLiteral("Local Runner")},
                                                    {QStringLiteral("endpoint"), QStringLiteral("Custom Endpoint")}};
    return (role == ProviderRole::Speech ? speech : refinement).value(id, id);
}

ProviderAttemptIssue speechIssue(const QString &id, Stage stage, ProviderFailureKind kind, bool offline = false)
{
    return {ProviderRole::Speech, id, stage, kind, QStringLiteral("reason"), offline};
}

ProviderAttemptIssue refinementIssue(const QString &id, ProviderFailureKind kind, bool offline = false)
{
    return {ProviderRole::Refinement, id, Stage::Prepare, kind, QStringLiteral("reason"), offline};
}

const ProviderFailureKind Network = ProviderFailureKind::Network;

// A session over fakes registered under the real ids and labels, with a
// chain per role. Every fake exists before the session starts, so a test can
// script it.
struct ChainRig {
    explicit ChainRig(const QStringList &speechChain, const QStringList &refinementChain = {QStringLiteral("none")})
    {
        settings.raw().clear();
        settings.setSpokenLanguage(QStringLiteral("en"));
        settings.setSpeechProvider(speechChain.first());
        settings.setSpeechFallbackProviders(speechChain.mid(1));
        settings.setRefinementProvider(refinementChain.first());
        settings.setRefinementFallbackProviders(refinementChain.mid(1));
        for (const QString &id : speechChain) {
            registerFakeSpeechProvider(registry, &speech[id], id, testLabel(ProviderRole::Speech, id));
            registry.speechProvider(id);
        }
        for (const QString &id : refinementChain) {
            if (id != QStringLiteral("none")) {
                registerFakeRefiner(registry, &refiners[id], id, testLabel(ProviderRole::Refinement, id));
                registry.refinementProvider(id);
            }
        }
        audio.onStart = [this] { ++microphoneStarts; };
        session = std::make_unique<DictationSession>(&settings, &audio, &media, &delivery, &registry);
    }

    void listen()
    {
        session->startListening();
        QTRY_COMPARE(session->state(), DictationState::Listening);
    }

    SettingsStore settings;
    FakeAudioInput audio;
    FakeMediaController media;
    FakeDelivery delivery;
    ProviderRegistry registry;
    std::map<QString, FakeSpeechTranscriber *> speech;
    std::map<QString, FakeRefiner *> refiners;
    int microphoneStarts = 0;
    std::unique_ptr<DictationSession> session;
};

} // namespace

Q_DECLARE_METATYPE(speecher::ProviderHistory)

class DictationSessionFallbacksTests : public QObject {
    Q_OBJECT

private slots:
    // The spec's outcome lines, with "Pasted" standing in for the receipt.
    void outcomeNamesTheFallbackThatDidTheWork_data()
    {
        QTest::addColumn<ProviderHistory>("history");
        QTest::addColumn<bool>("usedRawTranscript");
        QTest::addColumn<QString>("speechWarning");
        QTest::addColumn<QString>("message");
        QTest::addColumn<int>("outcome");
        QTest::addColumn<QString>("fixPage");

        const QString dropped = QStringLiteral("Part of the dictation may be missing. The connection dropped.");
        const int fallback = int(PopupOutcome::Fallback);
        QTest::newRow("no fallback")
            << ProviderHistory{{QStringLiteral("codex")}, {QStringLiteral("openai")}, {}} << false << dropped
            << QStringLiteral("Pasted • ") + dropped << int(PopupOutcome::Inserted) << QString();
        QTest::newRow("speech at start")
            << ProviderHistory{{QStringLiteral("endpoint")}, {},
                               {speechIssue(QStringLiteral("codex"), Stage::Prepare, ProviderFailureKind::Network)}}
            << false << QString()
            << QStringLiteral("Pasted • Transcribed with Custom Endpoint. ChatGPT Codex couldn't be reached.")
            << fallback << QString();
        QTest::newRow("offline, both roles")
            << ProviderHistory{{QStringLiteral("endpoint")}, {QStringLiteral("openai"), QStringLiteral("local")},
                               {speechIssue(QStringLiteral("codex"), Stage::Prepare, ProviderFailureKind::Network, true),
                                refinementIssue(QStringLiteral("openai"), ProviderFailureKind::Timeout, true)}}
            << false << QString()
            << QStringLiteral("Pasted • No internet, so Custom Endpoint and Local Runner did this one.") << fallback
            << QString();
        // A timeout the system's reachability does not explain is not offline.
        QTest::newRow("timeout while online")
            << ProviderHistory{{QStringLiteral("endpoint")}, {QStringLiteral("openai"), QStringLiteral("local")},
                               {speechIssue(QStringLiteral("codex"), Stage::Prepare, ProviderFailureKind::Network, true),
                                refinementIssue(QStringLiteral("openai"), ProviderFailureKind::Timeout)}}
            << false << QString()
            << QStringLiteral("Pasted • Transcribed with Custom Endpoint. ChatGPT Codex couldn't be reached. "
                              "Cleaned up with Local Runner. OpenAI didn't answer.")
            << fallback << QString();
        QTest::newRow("switched mid-dictation")
            << ProviderHistory{{QStringLiteral("codex"), QStringLiteral("local")}, {},
                               {speechIssue(QStringLiteral("codex"), Stage::Interrupted, ProviderFailureKind::Network)}}
            << false << dropped
            << QStringLiteral("Pasted • ChatGPT Codex dropped, so Local Model finished. A few words may be missing.")
            << fallback << QString();
        QTest::newRow("sign-in rejected")
            << ProviderHistory{{QStringLiteral("local")}, {},
                               {speechIssue(QStringLiteral("codex"), Stage::Prepare,
                                            ProviderFailureKind::Authentication)}}
            << false << QString() << QStringLiteral("Pasted • Used Local Model. Your ChatGPT sign-in has expired.")
            << fallback << QStringLiteral("accounts");
        QTest::newRow("refinement fallback")
            << ProviderHistory{{QStringLiteral("codex")}, {QStringLiteral("openai"), QStringLiteral("local")},
                               {refinementIssue(QStringLiteral("openai"), ProviderFailureKind::Network)}}
            << false << QString()
            << QStringLiteral("Pasted • Cleaned up with Local Runner. OpenAI couldn't be reached.") << fallback
            << QString();
        QTest::newRow("refinement chain exhausted")
            << ProviderHistory{{QStringLiteral("codex")}, {QStringLiteral("openai"), QStringLiteral("local")},
                               {refinementIssue(QStringLiteral("openai"), ProviderFailureKind::Network),
                                refinementIssue(QStringLiteral("local"), ProviderFailureKind::Network)}}
            << true << QString()
            << QStringLiteral("Used raw transcript • Pasted • OpenAI and Local Runner couldn't be reached.")
            << fallback << QString();
        QTest::newRow("raw transcript without a chain")
            << ProviderHistory{{QStringLiteral("codex")}, {QStringLiteral("openai")}, {}} << true << QString()
            << QStringLiteral("Used raw transcript • Pasted") << fallback << QString();
    }

    void outcomeNamesTheFallbackThatDidTheWork()
    {
        QFETCH(ProviderHistory, history);
        QFETCH(bool, usedRawTranscript);
        QFETCH(QString, speechWarning);
        const DictationOutcome outcome = dictationOutcome(QStringLiteral("Pasted"), false, history,
                                                          usedRawTranscript, speechWarning, testLabel);
        QTEST(outcome.message, "message");
        QTEST(int(outcome.outcome), "outcome");
        QTEST(outcome.fix.pageId, "fixPage");
    }

    // At start each provider is tried in turn, off the GUI thread when it
    // has a job for it, and the microphone opens once, for the one that
    // prepared.
    void speechStartsOnTheFirstProviderThatPrepares()
    {
        ChainRig rig({QStringLiteral("codex"), QStringLiteral("claude"), QStringLiteral("endpoint")});
        rig.speech[QStringLiteral("codex")]->prepareResult = {false, QStringLiteral("offline"), Network};
        rig.speech[QStringLiteral("claude")]->backgroundPrepare = true;
        rig.speech[QStringLiteral("claude")]->prepareResult = {false, QStringLiteral("expired"),
                                                              ProviderFailureKind::Authentication};
        QSignalSpy signIns(rig.session.get(), &DictationSession::providerSignInObserved);
        rig.listen();
        QCOMPARE(rig.speech[QStringLiteral("codex")]->startCalls, 0);
        QCOMPARE(rig.speech[QStringLiteral("claude")]->startCalls, 0);
        QCOMPARE(rig.speech[QStringLiteral("endpoint")]->startCalls, 1);
        QCOMPARE(rig.microphoneStarts, 1);
        // Turned down: noted for the settings status; a network failure says
        // nothing about the sign-in.
        QCOMPARE(signIns.size(), 1);
        QCOMPARE(signIns.first(), (QVariantList{QStringLiteral("claude"), false}));
    }

    void noSpeechProviderStartsAndTheErrorNamesEach()
    {
        ChainRig rig({QStringLiteral("codex"), QStringLiteral("endpoint"), QStringLiteral("local")});
        rig.speech[QStringLiteral("codex")]->prepareResult = {false, QStringLiteral("refused"), Network};
        rig.speech[QStringLiteral("endpoint")]->prepareResult = {false, QStringLiteral("slow"),
                                                                ProviderFailureKind::Timeout};
        rig.speech[QStringLiteral("local")]->prepareResult = {false, QStringLiteral("no model")};
        QSignalSpy errors(rig.session.get(), &DictationSession::popupErrorRequested);
        rig.session->startListening();
        QTRY_COMPARE(rig.session->state(), DictationState::Error);
        QCOMPARE(errors.last().at(0).toString(),
                 QStringLiteral("No speech service is available. ChatGPT Codex couldn't be reached, Custom Endpoint "
                                "didn't answer and Local Model has no model downloaded."));
        QCOMPARE(errors.last().at(1).value<PopupErrorAction>().pageId, QStringLiteral("dictation"));
        QCOMPARE(rig.microphoneStarts, 0);
    }

    // A key the keyring can't give fails its own provider, read on the
    // worker; the next provider still starts.
    void aSpeechKeyThatCantBeReadFailsOnlyItsProvider()
    {
        ChainRig rig({QStringLiteral("endpoint"), QStringLiteral("local")});
        rig.settings.raw().setValue(SettingsKeys::SecretsInKeyring, QStringList{QStringLiteral("speech-endpoint-key")});
        qputenv("SPEECHER_TEST_KEYRING_READ_TIMEOUT", "1");
        qputenv("SPEECHER_TEST_KEYRING_READ_DELAY_MS", "250");
        qputenv("SPEECHER_TEST_KEYRING_REFUSE_GUI_THREAD", "1");
        const auto restore = qScopeGuard([] {
            qunsetenv("SPEECHER_TEST_KEYRING_READ_TIMEOUT");
            qunsetenv("SPEECHER_TEST_KEYRING_READ_DELAY_MS");
            qunsetenv("SPEECHER_TEST_KEYRING_REFUSE_GUI_THREAD");
        });
        rig.session->startListening();
        QTRY_COMPARE_WITH_TIMEOUT(rig.session->state(), DictationState::Listening, 3000);
        QCOMPARE(rig.speech[QStringLiteral("endpoint")]->prepareCalls, 0);
        QCOMPARE(rig.speech[QStringLiteral("local")]->startCalls, 1);
        QVERIFY2(rig.settings.secrets()->lastError().isEmpty(), qPrintable(rig.settings.secrets()->lastError()));
    }

    // A fallback that can't listen for the Spoken Language, by its Local
    // Model's catalog entry, is passed over; one with no model is tried and
    // fails as unavailable.
    void aFallbackThatCantListenForTheSpokenLanguageIsPassedOver_data()
    {
        QTest::addColumn<QString>("modelId");
        QTest::addColumn<int>("localPrepares");
        QTest::newRow("English-only model") << QStringLiteral("moonshine-small") << 0;
        QTest::newRow("no model") << QString() << 1;
    }

    void aFallbackThatCantListenForTheSpokenLanguageIsPassedOver()
    {
        QFETCH(QString, modelId);
        QFETCH(int, localPrepares);
        ChainRig rig({QStringLiteral("codex"), QStringLiteral("local"), QStringLiteral("endpoint")});
        rig.settings.setSpokenLanguage(QStringLiteral("de"));
        LocalSpeechSettings local = rig.settings.localSpeechSettings();
        local.modelId = modelId;
        rig.settings.setLocalSpeechSettings(local);
        rig.speech[QStringLiteral("codex")]->prepareResult = {false, QStringLiteral("refused"), Network};
        rig.speech[QStringLiteral("local")]->prepareResult = {false, QStringLiteral("no model")};
        rig.listen();
        QCOMPARE(rig.speech[QStringLiteral("local")]->prepareCalls, localPrepares);
        QCOMPARE(rig.speech[QStringLiteral("local")]->startCalls, 0);
        QCOMPARE(rig.speech[QStringLiteral("endpoint")]->lastLanguage, QStringLiteral("de"));
    }

    // Guarded right after startAttempt(): a provider failing inside it never
    // gets the microphone, and the next one opens it.
    void aProviderFailingInsideStartAttemptNeverOpensTheMicrophoneForIt()
    {
        ChainRig rig({QStringLiteral("codex"), QStringLiteral("local")});
        FakeSpeechTranscriber *codex = rig.speech[QStringLiteral("codex")];
        FakeSpeechTranscriber *local = rig.speech[QStringLiteral("local")];
        codex->onStartAttempt = [codex] { codex->emitFailure(QStringLiteral("refused"), false, QStringLiteral("connect"), Network); };
        rig.audio.onStart = [&rig, local] {
            QCOMPARE(local->startCalls, 1);
            ++rig.microphoneStarts;
        };
        rig.listen();
        QCOMPARE(codex->cancelledAttempts, QList<quint64>{codex->currentAttemptId});
        QCOMPARE(rig.microphoneStarts, 1);
    }

    // Reconnects on the same provider come first, as without fallbacks; then
    // the next provider opens a fresh attempt with the words so far kept and
    // a reconnect budget of its own.
    void aDroppedProviderHandsOverAfterItsReconnects()
    {
        ChainRig rig({QStringLiteral("codex"), QStringLiteral("local")});
        FakeSpeechTranscriber *codex = rig.speech[QStringLiteral("codex")];
        FakeSpeechTranscriber *local = rig.speech[QStringLiteral("local")];
        rig.listen();
        codex->emitFinalText(QStringLiteral("first words"));
        for (int drop = 0; drop < 3; ++drop) {
            codex->emitFailure(QStringLiteral("dropped"), true, QStringLiteral("streaming"), Network);
        }
        QCOMPARE(codex->startCalls, 3);
        QCOMPARE(local->startCalls, 1);
        local->emitFailure(QStringLiteral("dropped"), true, QStringLiteral("streaming"), Network);
        QCOMPARE(local->startCalls, 2);
        local->emitFinalText(QStringLiteral("more words"));
        rig.session->stopListening();
        QTRY_COMPARE(rig.delivery.calls, 1);
        QCOMPARE(rig.delivery.lastText, QStringLiteral("first words more words"));
    }

    // A stream that keeps ending within seconds spends its reconnects and
    // hands over; one that ends after streaming a while rolls over on the
    // same provider.
    void aStreamEndingAtOnceHandsOverAndAStableOneRollsOver()
    {
        ChainRig rig({QStringLiteral("codex"), QStringLiteral("local")});
        FakeSpeechTranscriber *codex = rig.speech[QStringLiteral("codex")];
        rig.listen();
        const int stableMs = DictationSession::stableAttemptMs();
        DictationSession::setStableAttemptMs(0);
        codex->emitCompletion();
        DictationSession::setStableAttemptMs(stableMs);
        QCOMPARE(codex->startCalls, 2);
        QCOMPARE(rig.speech[QStringLiteral("local")]->startCalls, 0);
        for (int end = 0; end < 3; ++end) {
            codex->emitCompletion();
        }
        QCOMPARE(rig.speech[QStringLiteral("local")]->startCalls, 1);
    }

    // Audio heard while the next provider prepares waits in the session and
    // goes to it once, in order; what the failed provider got is never sent
    // again (rule A7).
    void audioHeardWhileTheNextProviderPreparesGoesToItOnce()
    {
        ChainRig rig({QStringLiteral("codex"), QStringLiteral("local")});
        FakeSpeechTranscriber *codex = rig.speech[QStringLiteral("codex")];
        FakeSpeechTranscriber *local = rig.speech[QStringLiteral("local")];
        local->backgroundPrepare = true;
        local->backgroundPrepareDelayMs = 100;
        rig.listen();
        rig.audio.pushAudio("a");
        codex->emitFailure(QStringLiteral("refused"), false, QStringLiteral("connect"), Network);
        rig.audio.pushAudio("b");
        rig.audio.pushAudio("c");
        QCOMPARE(local->startCalls, 0);
        QTRY_COMPARE(local->startCalls, 1);
        rig.audio.pushAudio("d");
        QCOMPARE(codex->audioChunks, QList<QByteArray>{"a"});
        QCOMPARE(local->audioChunks, (QList<QByteArray>{"b", "c", "d"}));
    }

    // The attempt id moves on before the old provider is cancelled, so what
    // it emits while stopping, and after, never reaches the transcript; its
    // partial is kept once. A whole-attempt transcript from the next one
    // replaces only its own words, and a late partial can't repaint the
    // preview once refinement has the text.
    void aRetiredProviderCannotReachTheSession()
    {
        ChainRig rig({QStringLiteral("codex"), QStringLiteral("local")}, {QStringLiteral("openai")});
        FakeSpeechTranscriber *codex = rig.speech[QStringLiteral("codex")];
        FakeSpeechTranscriber *local = rig.speech[QStringLiteral("local")];
        codex->onCancelAttempt = [codex](quint64) { codex->emitFinalText(QStringLiteral("ghost")); };
        rig.listen();
        codex->emitPartialText(QStringLiteral("half"));
        codex->emitFailure(QStringLiteral("refused"), false, QStringLiteral("streaming"), Network);
        codex->emitAttemptText(QStringLiteral("overwrite"));
        codex->emitFailure(QStringLiteral("late"), false, QStringLiteral("streaming"), Network);
        QCOMPARE(rig.session->state(), DictationState::Listening);
        local->emitFinalText(QStringLiteral("rest"));
        local->emitAttemptText(QStringLiteral("rest corrected"));
        rig.session->stopListening();
        QTRY_COMPARE(rig.session->state(), DictationState::Refining);
        QSignalSpy preview(rig.session.get(), &DictationSession::previewChanged);
        local->emitPartialText(QStringLiteral("late partial"));
        QCOMPARE(preview.size(), 0);
        QCOMPARE(rig.refiners[QStringLiteral("openai")]->lastRawTranscript, QStringLiteral("half rest corrected"));
    }

    // A provider failing to finish a paused attempt makes way for the next
    // without opening the microphone; the next attempt opens on resume, and
    // the paused one was told to finish once.
    void aProviderFailingWhilePausedHandsOverWithoutTheMicrophone()
    {
        ChainRig rig({QStringLiteral("codex"), QStringLiteral("local")});
        FakeSpeechTranscriber *codex = rig.speech[QStringLiteral("codex")];
        FakeSpeechTranscriber *local = rig.speech[QStringLiteral("local")];
        codex->autoCompleteOnFinish = false;
        rig.listen();
        codex->emitFinalText(QStringLiteral("before"));
        rig.session->pause();
        codex->emitFailure(QStringLiteral("expired"), false, QStringLiteral("finalize"),
                           ProviderFailureKind::Authentication);
        QCOMPARE(rig.session->state(), DictationState::Paused);
        QCOMPARE(local->startCalls, 0);
        QCOMPARE(rig.microphoneStarts, 1);
        rig.session->resume();
        QCOMPARE(local->startCalls, 1);
        QCOMPARE(rig.microphoneStarts, 2);
        local->emitFinalText(QStringLiteral("after"));
        rig.session->stopListening();
        QTRY_COMPARE(rig.delivery.calls, 1);
        QCOMPARE(rig.delivery.lastText, QStringLiteral("before after"));
        QCOMPARE(codex->stopCalls, 1);
    }

    // Audio heard after a quick resume, while the paused attempt still
    // finishes, goes to the next provider when that attempt fails.
    void wordsHeardAfterAQuickResumeGoToTheNextProvider()
    {
        ChainRig rig({QStringLiteral("codex"), QStringLiteral("local")});
        FakeSpeechTranscriber *codex = rig.speech[QStringLiteral("codex")];
        FakeSpeechTranscriber *local = rig.speech[QStringLiteral("local")];
        codex->autoCompleteOnFinish = false;
        rig.listen();
        rig.session->pause();
        rig.session->resume();
        rig.audio.pushAudio("x");
        codex->emitFailure(QStringLiteral("refused"), false, QStringLiteral("finalize"), Network);
        QCOMPARE(local->audioChunks, QList<QByteArray>{"x"});
        QVERIFY(codex->audioChunks.isEmpty());
        QCOMPARE(codex->stopCalls, 1);
        QCOMPARE(local->stopCalls, 0);
    }

    // The known limit: a provider failing after stop already had all the
    // audio, so no other provider is tried and the words that arrived are
    // refined, with today's warning.
    void aProviderFailingAtStopNeverHandsOver()
    {
        ChainRig rig({QStringLiteral("endpoint"), QStringLiteral("local")});
        FakeSpeechTranscriber *endpoint = rig.speech[QStringLiteral("endpoint")];
        endpoint->autoCompleteOnFinish = false;
        rig.listen();
        endpoint->emitFinalText(QStringLiteral("kept"));
        rig.session->stopListening();
        endpoint->emitFailure(QStringLiteral("upload failed"), false, QStringLiteral("finalize"),
                              ProviderFailureKind::Server);
        QTRY_COMPARE(rig.delivery.calls, 1);
        QCOMPARE(rig.delivery.lastText, QStringLiteral("kept"));
        QCOMPARE(rig.speech[QStringLiteral("local")]->prepareCalls, 0);
        QVERIFY(rig.session->lastMessage().contains(QStringLiteral("Part of the dictation may be missing")));
    }

    // Stopped with words from a quick resume still unsent: only those go to
    // the next provider, once, without the microphone, and refinement
    // follows once.
    void stoppingRoutesOnlyTheUnsentAudioToTheNextProvider()
    {
        ChainRig rig({QStringLiteral("codex"), QStringLiteral("local")});
        FakeSpeechTranscriber *codex = rig.speech[QStringLiteral("codex")];
        FakeSpeechTranscriber *local = rig.speech[QStringLiteral("local")];
        codex->autoCompleteOnFinish = false;
        rig.listen();
        codex->emitFinalText(QStringLiteral("paused words"));
        rig.session->pause();
        rig.session->resume();
        rig.audio.pushAudio("y");
        rig.session->stopListening();
        QCOMPARE(rig.session->state(), DictationState::Stopping);
        local->onStartAttempt = [local] { local->emitFinalText(QStringLiteral("resumed words")); };
        codex->emitFailure(QStringLiteral("refused"), false, QStringLiteral("finalize"), Network);
        QTRY_COMPARE(rig.delivery.calls, 1);
        QCOMPARE(local->audioChunks, QList<QByteArray>{"y"});
        QCOMPARE(local->stopCalls, 1);
        QCOMPARE(rig.microphoneStarts, 2);
        QCOMPARE(rig.delivery.lastText, QStringLiteral("paused words resumed words"));
    }

    // Stopped while the next provider prepares: it gets what was heard, then
    // refinement runs once. Cancelled instead: its preparation never applies
    // and nothing is delivered.
    void stopOrCancelWhileTheNextProviderPrepares_data()
    {
        QTest::addColumn<bool>("cancel");
        QTest::newRow("stop") << false;
        QTest::newRow("cancel") << true;
    }

    void stopOrCancelWhileTheNextProviderPrepares()
    {
        QFETCH(bool, cancel);
        ChainRig rig({QStringLiteral("codex"), QStringLiteral("local")});
        FakeSpeechTranscriber *codex = rig.speech[QStringLiteral("codex")];
        FakeSpeechTranscriber *local = rig.speech[QStringLiteral("local")];
        local->backgroundPrepare = true;
        local->backgroundPrepareDelayMs = 150;
        rig.listen();
        codex->emitFinalText(QStringLiteral("said"));
        codex->emitFailure(QStringLiteral("refused"), false, QStringLiteral("connect"), Network);
        rig.audio.pushAudio("z");
        if (cancel) {
            rig.session->cancel();
            QTest::qWait(400);
            QCOMPARE(local->prepareCalls, 0);
            QCOMPARE(local->startCalls, 0);
            QCOMPARE(rig.delivery.calls, 0);
            return;
        }
        rig.session->stopListening();
        QCOMPARE(rig.session->state(), DictationState::Stopping);
        QTRY_COMPARE(rig.delivery.calls, 1);
        QCOMPARE(local->audioChunks, QList<QByteArray>{"z"});
        QCOMPARE(local->stopCalls, 1);
        QTest::qWait(50);
        QCOMPARE(rig.delivery.calls, 1);
    }

    // An expired sign-in renews on the worker before refinement, and one that
    // can't renew fails the refiner there: prepare(), which would renew it on
    // the GUI thread, is never reached.
    void aRefinerRenewsItsSignInOffTheGuiThread()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("openai"));
        FakeAudioInput audio;
        FakeMediaController media;
        FakeDelivery delivery;
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        FakeRefiner *refiner = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        registerFakeRefiner(registry, &refiner);
        DictationSession session(&settings, &audio, &media, &delivery, &registry);
        session.startListening();
        QTRY_COMPARE(session.state(), DictationState::Listening);
        refiner->refreshRequired = true;
        refiner->backgroundRefresh = true;
        refiner->refreshResult = {false, QStringLiteral("refresh failed"), ProviderFailureKind::Network};
        speech->emitFinalText(QStringLiteral("spoken words"));
        session.stopListening();
        QCOMPARE(session.state(), DictationState::Refining);
        QTRY_COMPARE(delivery.calls, 1);
        QCOMPARE(refiner->backgroundRefreshCalls, 1);
        QCOMPARE(refiner->prepareCalls, 0);
        QCOMPARE(delivery.lastText, QStringLiteral("spoken words"));
    }

    void noSpeechServiceNamesEachProviderAndWhy()
    {
        QCOMPARE(noSpeechServiceText({speechIssue(QStringLiteral("codex"), Stage::Prepare, ProviderFailureKind::Network),
                                      speechIssue(QStringLiteral("endpoint"), Stage::Prepare,
                                                  ProviderFailureKind::Timeout),
                                      speechIssue(QStringLiteral("local"), Stage::Prepare,
                                                  ProviderFailureKind::Unavailable)},
                                     testLabel),
                 QStringLiteral("No speech service is available. ChatGPT Codex couldn't be reached, Custom Endpoint "
                                "didn't answer and Local Model has no model downloaded."));
    }
};

int runDictationSessionFallbacksTests(int argc, char **argv)
{
    DictationSessionFallbacksTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_dictation_session_fallbacks.moc"
