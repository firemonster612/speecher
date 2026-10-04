#include "common/test_prelude.h"
#include "common/test_doubles.h"

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
