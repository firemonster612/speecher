#include "common/test_suites.h"
#include "common/test_doubles.h"

#include "core/SettingsStore.h"
#include "dictation/DictationSession.h"
#include "transcribe/FileTranscriptionSession.h"

#include <QDir>
#include <QScopeGuard>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtEndian>

#include <cmath>

using namespace speecher;
using namespace speecher::test;

namespace {

// What the scripted transcribers saw. Owned by the test, because the session
// deletes each file's transcriber once the file is done.
struct Script {
    bool expireFirstStream = false;
    int attempts = 0;
    qsizetype bytes = 0;
};

// Answers a finished input with how many bytes of audio it received, and can
// end its first stream early the way a Codex session expires mid-file.
class ScriptedTranscriber final : public SpeechTranscriber {
public:
    ScriptedTranscriber(Script *script, QObject *parent)
        : SpeechTranscriber(parent)
        , m_script(script)
    {
    }

    QString id() const override { return QStringLiteral("claude"); }
    QString label() const override { return QStringLiteral("Scripted"); }
    bool requiresRefresh(const SpeechSettings &) const override { return false; }
    SpeechPrepareResult prepare(const SpeechSettings &) override { return {true, {}}; }

    void startAttempt(quint64, const SpeechSettings &) override
    {
        ++m_script->attempts;
        m_bytes = 0;
    }

    void sendAudio(quint64 attemptId, const QByteArray &pcm) override
    {
        m_script->bytes += pcm.size();
        m_bytes += pcm.size();
        if (m_script->expireFirstStream && m_script->attempts == 1) {
            emit finalTranscript(attemptId, QStringLiteral("before"));
            emit attemptCompleted(attemptId);
        }
    }

    void finishInput(quint64 attemptId) override
    {
        emit finalTranscript(attemptId, QStringLiteral("heard %1").arg(m_bytes));
        emit attemptCompleted(attemptId);
    }

    void cancelAttempt(quint64) override {}

private:
    Script *m_script;
    qsizetype m_bytes = 0;
};

// Half a second of a 440 Hz tone as 44.1 kHz stereo s16, so the file has to
// be downmixed and resampled on its way to the provider.
void writeWav(const QString &path)
{
    constexpr int rate = 44100;
    constexpr int channels = 2;
    QByteArray data;
    for (int i = 0; i < rate / 2; ++i) {
        const auto sample = qint16(8000 * std::sin(2 * M_PI * 440 * i / rate));
        for (int c = 0; c < channels; ++c) {
            data.append(reinterpret_cast<const char *>(&sample), 2);
        }
    }
    const auto le32 = [](quint32 v) { QByteArray b(4, 0); qToLittleEndian(v, b.data()); return b; };
    const auto le16 = [](quint16 v) { QByteArray b(2, 0); qToLittleEndian(v, b.data()); return b; };
    QByteArray wav = "RIFF" + le32(36 + data.size()) + "WAVEfmt " + le32(16) + le16(1) + le16(channels)
        + le32(rate) + le32(rate * channels * 2) + le16(channels * 2) + le16(16) + "data"
        + le32(data.size()) + data;
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(wav);
}

QString readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
}

class FileTranscriptionTests : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        SettingsStore().raw().clear();
        QVERIFY(m_dir.isValid());
        m_registry = std::make_unique<ProviderRegistry>();
        m_script = {};
        m_registry->registerSpeechProvider({QStringLiteral("claude"), QStringLiteral("Scripted")},
                                           [this](QObject *parent) {
                                               return new ScriptedTranscriber(&m_script, parent);
                                           });
        m_refinedWith.clear();
        m_registry->registerRefinementProvider({QStringLiteral("openai"), QStringLiteral("Fake")},
                                               [this](QObject *parent) {
                                                   auto *refiner = new FakeRefiner(parent);
                                                   connect(refiner, &TranscriptRefiner::completed, this, [this, refiner] {
                                                       m_refinedWith = {refiner->lastStyle, refiner->lastTone};
                                                   });
                                                   refiner->autoComplete = true;
                                                   refiner->autoCompleteText = QStringLiteral("Heard it.");
                                                   return refiner;
                                               });
    }

    void decodesStreamsRefinesAndSavesBesideTheInput()
    {
        const QString audio = m_dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        SettingsStore settings;
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        TranscribeOptions options = speechOnly();
        options.refinementProviderId = QStringLiteral("openai");
        options.cleanupStrength = QStringLiteral("strong_polish");
        options.tone = QStringLiteral("formal");

        QVERIFY(session.start({audio}, options));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);

        // Half a second at 16 kHz mono s16, give or take the resampler's edge.
        QVERIFY(qAbs(m_script.bytes - 16000) <= 8);
        const auto results = finished.first().first().value<QList<TranscribeFileResult>>();
        QCOMPARE(results.size(), 1);
        QVERIFY(results.first().raw.startsWith(QStringLiteral("heard ")));
        QCOMPARE(m_refinedWith, QStringList({QStringLiteral("strong_polish"), QStringLiteral("formal")}));
        QCOMPARE(results.first().refined, QStringLiteral("Heard it."));
        QCOMPARE(results.first().savedPath, m_dir.filePath(QStringLiteral("memo-transcribed.txt")));
        QCOMPARE(readFile(results.first().savedPath), QStringLiteral("Heard it."));
    }

    void rollsOverAStreamThatEndsMidFile()
    {
        DictationSession::setStableAttemptMs(0);
        const auto restore = qScopeGuard([] { DictationSession::setStableAttemptMs(10000); });
        m_script.expireFirstStream = true;
        const QString audio = m_dir.filePath(QStringLiteral("long.wav"));
        writeWav(audio);
        SettingsStore settings;
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);

        QVERIFY(session.start({audio}, speechOnly()));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);

        const auto results = finished.first().first().value<QList<TranscribeFileResult>>();
        QCOMPARE(m_script.attempts, 2);
        QVERIFY(results.first().raw.startsWith(QStringLiteral("before heard ")));
        QVERIFY(results.first().error.isEmpty());
    }

    void aFailedFileDoesNotStopTheBatch()
    {
        const QString broken = m_dir.filePath(QStringLiteral("broken.wav"));
        QFile file(broken);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("not audio at all");
        file.close();
        const QString audio = m_dir.filePath(QStringLiteral("good.wav"));
        writeWav(audio);
        SettingsStore settings;
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);

        QVERIFY(session.start({broken, audio}, speechOnly()));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);

        const auto results = finished.first().first().value<QList<TranscribeFileResult>>();
        QCOMPARE(results.size(), 2);
        QVERIFY(results.at(0).failed());
        QVERIFY(!results.at(0).error.isEmpty());
        QVERIFY(!results.at(1).failed());
        QCOMPARE(finished.first().at(1).toBool(), false);
    }

    void numbersASaveThatWouldOverwrite()
    {
        const QString audio = m_dir.filePath(QStringLiteral("talk.mp3.wav"));
        writeWav(audio);
        const QString folder = m_dir.filePath(QStringLiteral("out"));
        QVERIFY(QDir().mkpath(folder));
        for (const QString &taken : {QStringLiteral("talk.mp3-transcribed.txt"),
                                     QStringLiteral("talk.mp3-transcribed (2).txt")}) {
            QFile existing(QDir(folder).filePath(taken));
            QVERIFY(existing.open(QIODevice::WriteOnly));
        }
        SettingsStore settings;
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        TranscribeOptions options = speechOnly();
        options.destination = TranscriptDestination::Folder;
        options.folder = folder;

        QVERIFY(session.start({audio}, options));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);

        const auto results = finished.first().first().value<QList<TranscribeFileResult>>();
        QCOMPARE(results.first().savedPath,
                 QDir(folder).filePath(QStringLiteral("talk.mp3-transcribed (3).txt")));
        QVERIFY(readFile(results.first().savedPath).startsWith(QStringLiteral("heard ")));
    }

    void cancelSkipsTheRestAndStillFinishes()
    {
        const QString first = m_dir.filePath(QStringLiteral("one.wav"));
        const QString second = m_dir.filePath(QStringLiteral("two.wav"));
        writeWav(first);
        writeWav(second);
        SettingsStore settings;
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy started(&session, &FileTranscriptionSession::fileStarted);
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        QObject::connect(&session, &FileTranscriptionSession::fileProgress, &session,
                         [&session] { session.cancel(); });

        QVERIFY(session.start({first, second}, speechOnly()));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        QTest::qWait(200);

        QCOMPARE(started.count(), 1);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.first().at(1).toBool(), true);
        QVERIFY(finished.first().first().value<QList<TranscribeFileResult>>().isEmpty());
        QVERIFY(!session.isRunning());
        QVERIFY(!QFile::exists(m_dir.filePath(QStringLiteral("one-transcribed.txt"))));
    }

private:
    static TranscribeOptions speechOnly()
    {
        TranscribeOptions options;
        options.speechProviderId = QStringLiteral("claude");
        return options;
    }

    QTemporaryDir m_dir;
    std::unique_ptr<ProviderRegistry> m_registry;
    Script m_script;
    // Style and tone the last refinement ran with.
    QStringList m_refinedWith;
};

} // namespace

int runFileTranscriptionTests(int argc, char **argv)
{
    FileTranscriptionTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_file_transcription.moc"
