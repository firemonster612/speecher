#include "common/test_doubles.h"
#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "frontend/quick/host/QuickTranscription.h"
#include "providers/ProviderRegistry.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <cmath>

using namespace speecher;
using namespace speecher::test;

namespace {

// Half a second of a tone as 16 kHz mono s16.
void writeWav(const QString &path)
{
    QByteArray samples;
    for (int i = 0; i < 8000; ++i) {
        const auto sample = qint16(8000 * std::sin(i * 0.2));
        samples.append(reinterpret_cast<const char *>(&sample), 2);
    }
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(wavBytes(samples, 16000, 1));
}

} // namespace

// The UI's Transcription: the Transcribe page's queue as plain values, and
// its commands by the ids the page uses.
class QuickTranscriptionTests : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        SettingsStore().raw().clear();
        QVERIFY(m_dir.isValid());
    }

    void optionsReadAndChangeByTheirIds()
    {
        ApplicationController controller(true);
        QuickTranscription transcription(&controller);
        const auto option = [&transcription](const char *id) { return transcription.options().value(QLatin1String(id)); };

        QCOMPARE(option("destination").toString(), QStringLiteral("beside"));
        QCOMPARE(option("vocabulary").toBool(), true);
        QCOMPARE(option("language").toString(), controller.settings()->snapshot().speech.language);

        transcription.setOption(QStringLiteral("destination"), QStringLiteral("none"));
        transcription.setOption(QStringLiteral("language"), QStringLiteral("fr"));
        transcription.setOption(QStringLiteral("tone"), QStringLiteral("casual"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("no option destination taking")));
        transcription.setOption(QStringLiteral("destination"), QStringLiteral("nowhere"));

        QCOMPARE(option("destination").toString(), QStringLiteral("none"));
        QCOMPARE(option("language").toString(), QStringLiteral("fr"));
        QCOMPARE(option("tone").toString(), QStringLiteral("casual"));
    }

    // One folder takes the chooser's folder; dismissed before there is one,
    // the transcripts go beside each file instead.
    void theFolderChooserSetsWhereTranscriptsGo()
    {
        ApplicationController controller(true);
        QuickTranscription transcription(&controller);
        const auto option = [&transcription](const char *id) { return transcription.options().value(QLatin1String(id)); };

        transcription.setOption(QStringLiteral("destination"), QStringLiteral("folder"));
        transcription.setFolder({});
        QCOMPARE(option("destination").toString(), QStringLiteral("beside"));

        transcription.setOption(QStringLiteral("destination"), QStringLiteral("folder"));
        transcription.setFolder(QUrl::fromLocalFile(m_dir.path()));
        transcription.setFolder({});
        QCOMPARE(option("destination").toString(), QStringLiteral("folder"));
        QCOMPARE(option("folder").toString(), QDir::toNativeSeparators(m_dir.path()));
        QCOMPARE(option("folderUrl").toUrl(), QUrl::fromLocalFile(m_dir.path()));
    }

    // A refined batch's results show the refined text until Raw is chosen,
    // and Copy all heads each transcript with its file's name.
    void resultsShowTheChosenTranscriptAndCopyWithHeadings()
    {
        const QString first = m_dir.filePath(QStringLiteral("one.wav"));
        const QString second = m_dir.filePath(QStringLiteral("two.wav"));
        writeWav(first);
        writeWav(second);
        ApplicationController controller(true);
        useFakeProviders(controller);
        QuickTranscription transcription(&controller);
        transcription.addFiles({first, second});
        transcription.setOption(QStringLiteral("service"), QStringLiteral("claude"));
        transcription.setOption(QStringLiteral("refiner"), QStringLiteral("openai"));
        transcription.setOption(QStringLiteral("cleanup"), QStringLiteral("balanced"));
        transcription.setOption(QStringLiteral("destination"), QStringLiteral("none"));

        const QVariantMap file = transcription.files().first().toMap();
        QCOMPARE(file.value(QStringLiteral("name")).toString(), QStringLiteral("one.wav"));
        QCOMPARE(file.value(QStringLiteral("kind")).toString(), QStringLiteral("audio"));
        QVERIFY(file.value(QStringLiteral("bytes")).toLongLong() > 0);

        transcription.start();
        QCOMPARE(transcription.step(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(transcription.step(), 2, 10000);

        QCOMPARE(transcription.batch().value(QStringLiteral("refines")).toBool(), true);
        QCOMPARE(transcription.queue(), QStringList({QStringLiteral("done"), QStringLiteral("done")}));
        QVariantMap result = transcription.results().first().toMap();
        QCOMPARE(result.value(QStringLiteral("failed")).toBool(), false);
        QCOMPARE(result.value(QStringLiteral("text")).toString(), QStringLiteral("Heard it."));
        QCOMPARE(result.value(QStringLiteral("words")).toInt(), 2);

        transcription.setShowRaw(true);
        result = transcription.results().first().toMap();
        QCOMPARE(result.value(QStringLiteral("text")).toString(), QStringLiteral("heard it all"));
        transcription.copyAll();
        QCOMPARE(QGuiApplication::clipboard()->text(),
                 QStringLiteral("# one.wav\n\nheard it all\n\n\n# two.wav\n\nheard it all"));
    }

private:
    // Speech that hears "heard it all" in each file, and a refiner that
    // answers "Heard it.".
    static void useFakeProviders(ApplicationController &controller)
    {
        ProviderRegistry *registry = controller.providerRegistry();
        registry->registerSpeechProvider({QStringLiteral("claude"), QStringLiteral("Fake Speech")}, [](QObject *parent) {
            auto *speech = new FakeSpeechTranscriber(parent);
            speech->onStartAttempt = [speech] {
                emit speech->finalTranscript(speech->currentAttemptId, QStringLiteral("heard it all"));
            };
            return speech;
        });
        registry->registerRefinementProvider({QStringLiteral("openai"), QStringLiteral("Fake Refiner")},
                                             [](QObject *parent) {
                                                 auto *refiner = new FakeRefiner(parent);
                                                 refiner->autoComplete = true;
                                                 refiner->autoCompleteText = QStringLiteral("Heard it.");
                                                 return refiner;
                                             });
    }

    QTemporaryDir m_dir;
};

int runQuickTranscriptionTests(int argc, char **argv)
{
    QuickTranscriptionTests suite;
    return runTestSuite(&suite, argc, argv);
}

#include "test_quick_transcription.moc"
