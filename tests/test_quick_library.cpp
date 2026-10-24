#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "frontend/quick/host/QuickLibrary.h"
#include "frontend/quick/host/QuickSettings.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <memory>

using namespace speecher;

namespace {

QVariantMap profileItem(const QString &id, const QString &name)
{
    return {{QStringLiteral("id"), id}, {QStringLiteral("name"), name}, {QStringLiteral("cleanup"), QStringLiteral("balanced")},
            {QStringLiteral("tone"), QStringLiteral("none")}};
}

QString writeFile(const QTemporaryDir &dir, const char *name, const QByteArray &contents)
{
    const QString path = dir.filePath(QLatin1String(name));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size()) {
        qFatal("could not write %s", qPrintable(path));
    }
    return path;
}

QString toneOf(const QString &profile)
{
    return writingProfileSettingsFor(SettingsStore().writingProfileSettings(), profile).tone;
}

void ignoreRefusal(const char *command)
{
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^Library\\.%1\\(").arg(QLatin1String(command))));
}

} // namespace

// The UI's LibraryLists: the profiles, tones, levels, vocabulary and app
// matches, read from and saved through the daemon's store, as another store
// (the CLI's) reads them.
class QuickLibraryTests : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        SettingsStore().raw().clear();
        QTest::failOnWarning();
    }

    void addedProfilesFollowTheBuiltInsAndNeedAFreeName()
    {
        ApplicationController controller(true);
        QuickLibrary library(&controller);
        QCOMPARE(library.profiles().size(), 5);
        QCOMPARE(library.profiles().first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Work"));

        const QString id = library.newId(QStringLiteral("custom_profile"));
        QVERIFY(library.add(QStringLiteral("profiles"), profileItem(id, QStringLiteral("Spanish replies"))));
        QVERIFY(library.update(QStringLiteral("profiles"), 5, {{QStringLiteral("language"), QStringLiteral(" Spanish ")}}));

        const WritingProfileSettings saved = writingProfileSettingsFor(SettingsStore().writingProfileSettings(), id);
        QCOMPARE(saved.name, QStringLiteral("Spanish replies"));
        QCOMPARE(saved.outputLanguage, QStringLiteral("Spanish"));
        const QVariantMap shown = library.profiles().last().toMap();
        QCOMPARE(shown.value(QStringLiteral("id")).toString(), id);
        QVERIFY(!shown.value(QStringLiteral("builtIn")).toBool());

        ignoreRefusal("add");
        QVERIFY(!library.add(QStringLiteral("profiles"),
                             profileItem(library.newId(QStringLiteral("custom_profile")), QStringLiteral("spanish REPLIES"))));
        ignoreRefusal("update");
        QVERIFY(!library.update(QStringLiteral("profiles"), 0, {{QStringLiteral("name"), QStringLiteral("Job")}}));
        ignoreRefusal("update");
        QVERIFY(!library.update(QStringLiteral("profiles"), 0, {{QStringLiteral("cleanup"), QStringLiteral("made_up")}}));
        QCOMPARE(SettingsStore().writingProfileSettings().size(), 6);
    }

    void removingAToneMovesItsProfilesUntilUndone()
    {
        ApplicationController controller(true);
        QuickLibrary library(&controller);
        const QString tone = library.newId(QStringLiteral("custom_tone"));
        QVERIFY(library.add(QStringLiteral("customTones"), {{QStringLiteral("id"), tone},
                                                            {QStringLiteral("name"), QStringLiteral("Dry")},
                                                            {QStringLiteral("instruction"), QStringLiteral("Deadpan.")}}));
        QVERIFY(library.update(QStringLiteral("profiles"), 0, {{QStringLiteral("tone"), tone}}));
        QCOMPARE(toneOf(WritingProfile::Work), tone);

        const int undo = library.removeTone(tone);
        QVERIFY(undo >= 0);
        QVERIFY(SettingsStore().customTones().isEmpty());
        QCOMPARE(toneOf(WritingProfile::Work), QStringLiteral("none"));

        library.undo(undo);
        QCOMPARE(SettingsStore().customTones().size(), 1);
        QCOMPARE(toneOf(WritingProfile::Work), tone);
        // A token works once.
        library.undo(undo);
        QCOMPARE(SettingsStore().customTones().size(), 1);
    }

    void removingAProfileResetsTheFallbackSetting()
    {
        ApplicationController controller(true);
        QuickLibrary library(&controller);
        QuickSettings settings(&controller);
        const QString id = library.newId(QStringLiteral("custom_profile"));
        QVERIFY(library.add(QStringLiteral("profiles"), profileItem(id, QStringLiteral("Notes"))));
        settings.set(QStringLiteral("defaultWritingProfile"), id);
        QVERIFY(library.add(QStringLiteral("recognitionRules"),
                            {{QStringLiteral("match"), QStringLiteral("obsidian")}, {QStringLiteral("profile"), id}}));
        QCOMPARE(SettingsStore().defaultWritingProfile(), id);

        library.removeProfile(id);
        QCOMPARE(SettingsStore().defaultWritingProfile(), WritingProfile::Other);
        QCOMPARE(settings.values().value(QStringLiteral("defaultWritingProfile")).toString(), WritingProfile::Other);
        QVERIFY(SettingsStore().appRecognitionRules().isEmpty());
        QCOMPARE(library.profiles().size(), 5);
    }

    void editingATermKeepsWhatTheListDoesNotShow()
    {
        {
            SettingsStore store;
            VocabularyEntry learned{QStringLiteral("Ngozi"), QStringLiteral("learned")};
            learned.frequency = 17;
            learned.lastUsedMs = 1700000000000;
            store.setVocabularyEntries({learned});
        }
        ApplicationController controller(true);
        QuickLibrary library(&controller);
        QCOMPARE(library.terms().first().toMap().value(QStringLiteral("uses")).toInt(), 17);

        QVERIFY(library.update(QStringLiteral("terms"), 0,
                               {{QStringLiteral("keyTerm"), false}, {QStringLiteral("context"), QStringLiteral("A name.")},
                                {QStringLiteral("profiles"), QStringList{QStringLiteral("work"), QStringLiteral("gone")}}}));
        const VocabularyEntry saved = SettingsStore().vocabularyEntries().first();
        QCOMPARE(saved.frequency, 17);
        QCOMPARE(saved.lastUsedMs, qint64(1700000000000));
        QCOMPARE(saved.source, QStringLiteral("learned"));
        QVERIFY(!saved.keyTerm);
        QCOMPARE(saved.context, QStringLiteral("A name."));
        QCOMPARE(saved.profiles, QStringList{QStringLiteral("work")});

        ignoreRefusal("add");
        QVERIFY(!library.add(QStringLiteral("terms"), {{QStringLiteral("term"), QStringLiteral(" ngozi ")}}));
        ignoreRefusal("update");
        QVERIFY(!library.update(QStringLiteral("terms"), 0, {{QStringLiteral("uses"), 99}}));

        const int undo = library.remove(QStringLiteral("terms"), 0);
        QVERIFY(SettingsStore().vocabularyEntries().isEmpty());
        library.undo(undo);
        QCOMPARE(SettingsStore().vocabularyEntries().first().frequency, 17);
    }

    void appMatchesTakeOverTheOldPerAppOverrides()
    {
        SettingsStore().setWritingProfileOverrides({{QStringLiteral("org.kde.kate"), WritingProfile::Email, true}});
        ApplicationController controller(true);
        QuickLibrary library(&controller);
        QCOMPARE(library.recognitionRules().size(), 1);
        QCOMPARE(library.recognitionRules().first().toMap().value(QStringLiteral("profile")).toString(), WritingProfile::Email);
        QVERIFY(!library.builtInRecognitionRules().isEmpty());

        QVERIFY(library.add(QStringLiteral("recognitionRules"),
                            {{QStringLiteral("match"), QStringLiteral("konsole")}, {QStringLiteral("category"), QStringLiteral("terminal")}}));
        SettingsStore another;
        QVERIFY(another.writingProfileOverrides().isEmpty());
        QCOMPARE(another.appRecognitionRules().size(), 2);
        QCOMPARE(another.appRecognitionRules().last().category, AppCategory::Terminal);

        ignoreRefusal("add");
        QVERIFY(!library.add(QStringLiteral("recognitionRules"), {{QStringLiteral("match"), QStringLiteral("Konsole")},
                                                                  {QStringLiteral("profile"), WritingProfile::Work}}));
        ignoreRefusal("update");
        QVERIFY(!library.update(QStringLiteral("recognitionRules"), 1, {{QStringLiteral("category"), QString()}}));
    }

    void importsAddWhatIsNewAndSayWhyAFileFailed()
    {
        QTemporaryDir dir;
        ApplicationController controller(true);
        QuickLibrary library(&controller);
        QVERIFY(library.add(QStringLiteral("terms"), {{QStringLiteral("term"), QStringLiteral("Grafana")}}));

        const QString csv = writeFile(dir, "terms.csv", "term\nGrafana\nPriya Raman\n");
        const QVariantMap terms = library.importTerms(csv);
        QCOMPARE(terms.value(QStringLiteral("added")).toInt(), 1);
        QCOMPARE(terms.value(QStringLiteral("error")).toString(), QString());
        QCOMPARE(SettingsStore().vocabularyEntries().size(), 2);

        const QVariantMap broken = library.importTerms(writeFile(dir, "broken.csv", "term\n\"Unclosed\n"));
        QVERIFY(!broken.value(QStringLiteral("error")).toString().isEmpty());
        QVERIFY(!library.importTerms(dir.filePath(QStringLiteral("missing.csv"))).value(QStringLiteral("error")).toString().isEmpty());

        const QString json = writeFile(dir, "snippets.json", R"([{"phrase": "sign off", "replacement": "Thanks"}])");
        QCOMPARE(library.importReplacements(QUrl::fromLocalFile(json).toString()).value(QStringLiteral("added")).toInt(), 1);
        QCOMPARE(library.importReplacements(json).value(QStringLiteral("added")).toInt(), 0);
        QCOMPARE(SettingsStore().bindingRules().size(), 1);
    }

    void learnedCorrectionsShowAsTheyAreLearnedAndCanBeTakenBack()
    {
        ApplicationController controller(true);
        QuickLibrary library(&controller);
        QSignalSpy changed(&library, &QuickLibrary::listsChanged);
        QVERIFY(controller.settings()->recordCorrectionEvidence({QStringLiteral("speaker"), QStringLiteral("Speecher"), 0.95},
                                                                QStringLiteral("slack")));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(library.corrections().first().toMap().value(QStringLiteral("corrected")).toString(), QStringLiteral("Speecher"));

        QVERIFY(library.update(QStringLiteral("corrections"), 0, {{QStringLiteral("enabled"), false}}));
        QVERIFY(!SettingsStore().learnedCorrections().first().enabled);

        const int undo = library.undoLastCorrection();
        QVERIFY(SettingsStore().learnedCorrections().isEmpty());
        library.undo(undo);
        QCOMPARE(SettingsStore().learnedCorrections().first().original, QStringLiteral("speaker"));
    }

    // The app's Library is Library.qml over this type: its wording reads the
    // real lists.
    void libraryInTheUiReadsTheRealLists()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        const QuickLibrary::ControllerScope scope(&controller);
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport Speecher.Backend\nQtObject {\n"
                          "    property int profiles: Library.profiles.length\n"
                          "    property string summary: Library.profileSummary(Library.profiles[1])\n"
                          "    function addTerm() { return Library.add(\"terms\", { term: \"Wayland\" }) }\n"
                          "}\n",
                          QUrl());
        const std::unique_ptr<QObject> probe(component.create());
        QVERIFY2(probe, qPrintable(component.errorString()));
        QCOMPARE(probe->property("profiles").toInt(), 5);
        QCOMPARE(probe->property("summary").toString(), QStringLiteral("Medium cleanup"));

        QVariant added;
        QMetaObject::invokeMethod(probe.get(), "addTerm", Q_RETURN_ARG(QVariant, added));
        QVERIFY(added.toBool());
        QCOMPARE(SettingsStore().vocabularyEntries().first().term, QStringLiteral("Wayland"));
    }
};

int runQuickLibraryTests(int argc, char **argv)
{
    QuickLibraryTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_quick_library.moc"
