#include "common/test_doubles.h"
#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#include "core/settings/SettingsKeys.h"
#include "frontend/quick/host/QuickTranscription.h"
#include "providers/ProviderRegistry.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <cmath>
#include <functional>
#include <memory>

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
        QCOMPARE(option("language").toString(), controller.settings()->dictationSnapshot().speech.language);

        transcription.setOption(QStringLiteral("destination"), QStringLiteral("none"));
        transcription.setOption(QStringLiteral("language"), QStringLiteral("fr"));
        transcription.setOption(QStringLiteral("tone"), QStringLiteral("casual"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("no option destination taking")));
        transcription.setOption(QStringLiteral("destination"), QStringLiteral("nowhere"));

        QCOMPARE(option("destination").toString(), QStringLiteral("none"));
        QCOMPARE(option("language").toString(), QStringLiteral("fr"));
        QCOMPARE(option("tone").toString(), QStringLiteral("casual"));
    }

    // The page reads the options on the GUI thread, so they leave a key only
    // the keyring holds unread.
    void optionsNeverWaitForTheKeyring()
    {
        SettingsStore().raw().setValue(SettingsKeys::SecretsInKeyring, QStringList{QStringLiteral("speech-endpoint-key")});
        qputenv("SPEECHER_TEST_KEYRING_REFUSE_GUI_THREAD", "1");
        const auto restore = qScopeGuard([] { qunsetenv("SPEECHER_TEST_KEYRING_REFUSE_GUI_THREAD"); });
        ApplicationController controller(true);
        QuickTranscription transcription(&controller);

        transcription.options();

        QCOMPARE(controller.settings()->secrets()->readFailure(SecretStore::Secret::SpeechEndpointKey), QString());
    }

    // A model chosen for the batch is the one its service runs, until another
    // service is chosen; choosing the same one again keeps it, and the
    // settings keep their own.
    void aBatchModelLastsUntilAnotherServiceIsChosen()
    {
        ApplicationController controller(true);
        QuickTranscription transcription(&controller);
        const AppSettings saved = controller.settings()->snapshot();

        transcription.setOption(QStringLiteral("refiner"), QStringLiteral("endpoint"));
        transcription.setOption(QStringLiteral("refinerModel"), QStringLiteral("qwen3:8b"));
        transcription.setOption(QStringLiteral("refiner"), QStringLiteral("endpoint"));
        QCOMPARE(transcription.refinerModel(), QStringLiteral("qwen3:8b"));
        transcription.setOption(QStringLiteral("refiner"), QStringLiteral("anthropic"));
        QCOMPARE(transcription.refinerModel(), saved.refinement.anthropicModel);

        transcription.setOption(QStringLiteral("service"), QStringLiteral("endpoint"));
        transcription.setOption(QStringLiteral("speechModel"), QStringLiteral("whisper-large-v3"));
        transcription.setOption(QStringLiteral("service"), QStringLiteral("endpoint"));
        QCOMPARE(transcription.speechModel(), QStringLiteral("whisper-large-v3"));
        transcription.setOption(QStringLiteral("service"), QStringLiteral("claude"));
        QVERIFY(transcription.speechModel().isEmpty());

        QCOMPARE(controller.settings()->snapshot().refinement.endpoint.model, saved.refinement.endpoint.model);
        QCOMPARE(controller.settings()->snapshot().speech.endpoint.model, saved.speech.endpoint.model);
    }

    // A picked suggestion fills the model field as typing would, so the field
    // still shows the batch's model when another service resets it.
    void aPickedSuggestionKeepsTheFieldFollowingItsModel()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import Speecher.Controls
            Item {
                property string model: "gpt-5.5"
                property alias field: field
                SuggestField {
                    id: field
                    text: parent.model
                    suggestions: [{ id: "claude-opus-5-5", label: "Claude Opus 5.5" }]
                    onCommitted: v => parent.model = v
                }
            })",
                          QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *field = root->property("field").value<QObject *>();
        QObject *menu = nullptr;
        for (QObject *child : field->findChildren<QObject *>()) {
            if (QLatin1String(child->metaObject()->className()).startsWith(QLatin1String("Menu_"))) {
                menu = child;
            }
        }
        QVERIFY(menu);

        QMetaObject::invokeMethod(menu, "picked", Q_ARG(QString, QStringLiteral("claude-opus-5-5")));
        QCOMPARE(root->property("model").toString(), QStringLiteral("claude-opus-5-5"));
        QCOMPARE(field->property("text").toString(), QStringLiteral("claude-opus-5-5"));
        root->setProperty("model", QStringLiteral("gpt-5.5"));
        QCOMPARE(field->property("text").toString(), QStringLiteral("gpt-5.5"));
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

    // Words heard so far change only the partial, so the file's waveform
    // and queue aren't made again for each of them.
    void aPartialTranscriptChangesOnlyThePartial()
    {
        const QString audio = m_dir.filePath(QStringLiteral("one.wav"));
        writeWav(audio);
        ApplicationController controller(true);
        QuickTranscription transcription(&controller);
        QSignalSpy fileChanges(&transcription, &QuickTranscription::fileChanged);
        int fileChangesWithThePartial = -1;
        QString partial;
        useFakeProviders(controller, [&](FakeSpeechTranscriber *speech) {
            const int before = int(fileChanges.count());
            emit speech->partialTranscript(speech->currentAttemptId, QStringLiteral("heard it"));
            fileChangesWithThePartial = int(fileChanges.count()) - before;
            partial = transcription.partial();
            emit speech->finalTranscript(speech->currentAttemptId, QStringLiteral("heard it all"));
        });
        transcription.addFiles({audio});
        transcription.setOption(QStringLiteral("service"), QStringLiteral("claude"));
        transcription.setOption(QStringLiteral("destination"), QStringLiteral("none"));

        transcription.start();
        QTRY_COMPARE_WITH_TIMEOUT(transcription.step(), 2, 10000);
        QCOMPARE(partial, QStringLiteral("heard it"));
        QCOMPARE(fileChangesWithThePartial, 0);
    }

    // Export offers to save a result beside its recording, in the formats
    // it has, and Export all confirms only what it wrote.
    void exportSavesBesideTheRecordingAndConfirmsOnlyWhatItWrote()
    {
        const QString audio = m_dir.filePath(QStringLiteral("one.wav"));
        writeWav(audio);
        ApplicationController controller(true);
        useFakeProviders(controller);
        QuickTranscription transcription(&controller);
        transcription.addFiles({audio});
        transcription.setOption(QStringLiteral("service"), QStringLiteral("claude"));
        transcription.setOption(QStringLiteral("destination"), QStringLiteral("none"));
        transcription.start();
        QTRY_COMPARE_WITH_TIMEOUT(transcription.step(), 2, 10000);

        QCOMPARE(transcription.exportFile(0, QStringLiteral("text")),
                 QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("one-transcribed.txt"))));
        // The fake speech gives no timings to make subtitles of.
        QCOMPARE(transcription.exportFile(0, QStringLiteral("srt")), QUrl());

        QTemporaryDir folder;
        QSignalSpy exported(&transcription, &QuickTranscription::exported);
        transcription.exportAll(QStringLiteral("srt"), QUrl::fromLocalFile(folder.path()));
        QCOMPARE(exported.count(), 0);
        QVERIFY(QDir(folder.path()).isEmpty());
        transcription.exportAll(QStringLiteral("text"), QUrl::fromLocalFile(folder.path()));
        QCOMPARE(exported.count(), 1);
        QCOMPARE(exported.first().first().toInt(), -1);
        QCOMPARE(QDir(folder.path()).entryList(QDir::Files).size(), 1);
    }

private:
    // Speech that hears "heard it all" in each file, or does what hear says,
    // and a refiner that answers "Heard it.".
    static void useFakeProviders(ApplicationController &controller,
                                 std::function<void(FakeSpeechTranscriber *)> hear = {})
    {
        ProviderRegistry *registry = controller.providerRegistry();
        registry->registerSpeechProvider({QStringLiteral("claude"), QStringLiteral("Fake Speech")}, [hear](QObject *parent) {
            auto *speech = new FakeSpeechTranscriber(parent);
            speech->onStartAttempt = [speech, hear] {
                if (hear) {
                    hear(speech);
                    return;
                }
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
