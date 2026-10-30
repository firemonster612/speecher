#include "common/test_suites.h"
#include "platform/audio/SilenceGate.h"

using namespace speecher;

namespace {

class SilenceGateTests : public QObject {
    Q_OBJECT

private slots:
    void leadsSpeechWithThePreRoll()
    {
        // Sizes in bytes: an 8-byte pre-roll and a 4-byte post-roll.
        SilenceGate gate;
        gate.setSizes(8, 4);
        QVERIFY(gate.pass("aaaaaa", false).isEmpty());
        QVERIFY(gate.pass("bbbbbb", false).isEmpty());

        // The newest quiet leads in, then the whole voiced chunk, though it
        // is longer than the pre-roll.
        const QByteArray speech(20, 's');
        QCOMPARE(gate.pass(speech, true), (QList<QByteArray>{"aabbbbbb", speech}));
    }

    void trailsSpeechWithThePostRoll()
    {
        SilenceGate gate;
        gate.setSizes(8, 6);
        gate.pass("ss", true);

        // A pause shorter than the post-roll is sent once speech resumes.
        QVERIFY(gate.pass("qq", false).isEmpty());
        QCOMPARE(gate.pass("ss", true), (QList<QByteArray>{"qq", "ss"}));

        // Quiet that fills the post-roll sends that much, then the gate
        // holds quiet back again.
        QVERIFY(gate.pass("abcd", false).isEmpty());
        QCOMPARE(gate.pass("efgh", false), (QList<QByteArray>{"abcdef"}));
        QVERIFY(gate.pass("ijkl", false).isEmpty());

        // Resumed speech is led in only by quiet the post-roll didn't send.
        QCOMPARE(gate.pass("ss", true), (QList<QByteArray>{"ghijkl", "ss"}));
    }
};

} // namespace

int runSilenceGateTests(int argc, char **argv)
{
    SilenceGateTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_silence_gate.moc"
