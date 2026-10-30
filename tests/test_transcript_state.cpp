#include "common/test_suites.h"
#include "core/TranscriptState.h"

using namespace speecher;


class TranscriptStateTests : public QObject {
    Q_OBJECT

private slots:
    void transcriptStateMerges()
    {
        TranscriptState state;
        state.setPartial(QStringLiteral("hello"));
        QCOMPARE(state.text(), QStringLiteral("hello"));
        state.commitFinal(QStringLiteral("hello"));
        QCOMPARE(state.text(), QStringLiteral("hello"));
        state.setPartial(QStringLiteral("world"));
        QCOMPARE(state.text(), QStringLiteral("hello world"));
    }

    void transcriptStateReplacesFinalsWithAuthoritativeText()
    {
        TranscriptState state;
        state.commitFinal(QStringLiteral("hello"));
        state.commitFinal(QStringLiteral("wrold"));
        state.setPartial(QStringLiteral("stale"));

        state.replaceFinals(QStringLiteral("hello world"));
        QCOMPARE(state.text(), QStringLiteral("hello world"));

        state.replaceFinals(QStringLiteral("   "));
        QCOMPARE(state.text(), QStringLiteral("hello world"));
    }

    void transcriptStatePreservesRepeatedFinalSegments()
    {
        TranscriptState state;
        state.commitFinal(QStringLiteral("yes"));
        state.commitFinal(QStringLiteral("yes"));

        QCOMPARE(state.text(), QStringLiteral("yes yes"));
    }

    // The language the kept words were heard in: a partial counts while it
    // shows, and one taken back leaves the earlier attempt's.
    void heardLanguageFollowsTheWordsKept()
    {
        TranscriptState state;
        state.startAttempt(QStringLiteral("de"));
        state.commitFinal(QStringLiteral("hallo"));
        state.startAttempt(QStringLiteral("fr"));
        state.setPartial(QStringLiteral("euh"));
        QCOMPARE(state.heardLanguage(), QStringLiteral("fr"));
        state.setPartial(QString());
        QCOMPARE(state.heardLanguage(), QStringLiteral("de"));
    }
};

int runTranscriptStateTests(int argc, char **argv)
{
    TranscriptStateTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_transcript_state.moc"
