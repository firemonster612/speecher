#include "common/test_doubles.h"
#include "common/test_http.h"
#include "common/test_auth.h"

using namespace speecher::test;


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

    void transcriptStatePreservesRepeatedFinalSegments()
    {
        TranscriptState state;
        state.commitFinal(QStringLiteral("yes"));
        state.commitFinal(QStringLiteral("yes"));

        QCOMPARE(state.text(), QStringLiteral("yes yes"));
    }
};

int runTranscriptStateTests(int argc, char **argv)
{
    TranscriptStateTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_transcript_state.moc"
