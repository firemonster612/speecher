#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "frontend/quick/host/QuickLibrary.h"
#include "frontend/quick/host/QuickModels.h"
#include "frontend/quick/host/QuickNavigation.h"
#include "frontend/quick/host/QuickSettings.h"
#include "frontend/quick/host/QuickTranscription.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>

#include <algorithm>
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
    return writingProfileSettingsFor(SettingsStore().dictationSnapshot().refinement.writingProfiles, profile).tone;
}

// What add and update return when they save.
const QString saved;

VocabularyEntry termEntry(const QString &term, const QString &context = {})
{
    VocabularyEntry entry{term};
    entry.context = context;
    return entry;
}

const VocabularyEntry *findTerm(const QList<VocabularyEntry> &terms, const QString &term)
{
    const auto found = std::find_if(terms.cbegin(), terms.cend(), [&term](const VocabularyEntry &entry) { return entry.term == term; });
    return found == terms.cend() ? nullptr : &*found;
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
        failOnUnexpectedWarnings();
    }

    void addedProfilesFollowTheBuiltInsAndNeedAFreeName()
    {
        ApplicationController controller(true);
        QuickLibrary library(&controller);
        QCOMPARE(library.profiles().size(), 5);
        QCOMPARE(library.profiles().first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Work"));

        const QString id = library.newId(QStringLiteral("custom_profile"));
        QCOMPARE(library.add(QStringLiteral("profiles"), profileItem(id, QStringLiteral("Spanish replies"))), saved);
        QCOMPARE(library.update(QStringLiteral("profiles"), id, {{QStringLiteral("language"), QStringLiteral(" Spanish ")}}), saved);

        const WritingProfileSettings stored = writingProfileSettingsFor(SettingsStore().dictationSnapshot().refinement.writingProfiles, id);
        QCOMPARE(stored.name, QStringLiteral("Spanish replies"));
        QCOMPARE(stored.outputLanguage, QStringLiteral("Spanish"));
        const QVariantMap shown = library.profiles().last().toMap();
        QCOMPARE(shown.value(QStringLiteral("id")).toString(), id);
        QVERIFY(!shown.value(QStringLiteral("builtIn")).toBool());
        QCOMPARE(SettingsStore().dictationSnapshot().refinement.writingProfiles.size(), 6);
    }

    // A form's save is refused with the reason it shows.
    void eachRefusalSaysWhy()
    {
        ApplicationController controller(true);
        QuickLibrary library(&controller);
        const QString profiles = QStringLiteral("profiles");
        const QString id = library.newId(QStringLiteral("custom_profile"));
        QCOMPARE(library.add(profiles, profileItem(id, QStringLiteral("Notes"))), saved);
        QCOMPARE(library.add(profiles, profileItem(library.newId(QStringLiteral("custom_profile")), QStringLiteral(" notes "))),
                 QStringLiteral("taken"));
        QCOMPARE(library.update(profiles, id, {{QStringLiteral("name"), QStringLiteral("work")}}), QStringLiteral("taken"));
        QCOMPARE(library.update(profiles, id, {{QStringLiteral("name"), QStringLiteral("  ")}}), QStringLiteral("blank"));
        QCOMPARE(library.update(profiles, WritingProfile::Work, {{QStringLiteral("name"), QStringLiteral("Job")}}),
                 QStringLiteral("builtIn"));
        QCOMPARE(library.update(profiles, id, {{QStringLiteral("tone"), QStringLiteral("custom_tone_gone")}}),
                 QStringLiteral("missing"));
        QCOMPARE(library.update(profiles, QStringLiteral("custom_profile_gone"), {{QStringLiteral("language"), QString()}}),
                 QStringLiteral("missing"));

        const QString tones = QStringLiteral("customTones");
        QCOMPARE(library.add(tones, {{QStringLiteral("id"), library.newId(QStringLiteral("custom_tone"))},
                                     {QStringLiteral("name"), QStringLiteral("Dry")}}),
                 QStringLiteral("incomplete"));
        const QString levels = QStringLiteral("customCleanupLevels");
        QCOMPARE(library.add(levels, {{QStringLiteral("id"), library.newId(QStringLiteral("custom_level"))},
                                      {QStringLiteral("name"), QStringLiteral("Raw")},
                                      {QStringLiteral("base"), kCustomOnlyCleanupBase}}),
                 QStringLiteral("incomplete"));

        const QString replacements = QStringLiteral("replacements");
        QCOMPARE(library.add(replacements, {{QStringLiteral("phrase"), QStringLiteral("sign off")},
                                            {QStringLiteral("replacement"), QStringLiteral("Thanks")}}),
                 saved);
        QCOMPARE(library.add(replacements, {{QStringLiteral("phrase"), QStringLiteral("Sign-off!")},
                                            {QStringLiteral("replacement"), QStringLiteral("Bye")}}),
                 QStringLiteral("taken"));
        QCOMPARE(library.add(replacements, {{QStringLiteral("phrase"), QStringLiteral("?!")},
                                            {QStringLiteral("replacement"), QStringLiteral("Bye")}}),
                 QStringLiteral("blank"));
        QCOMPARE(library.add(replacements, {{QStringLiteral("phrase"), QStringLiteral("my phone")},
                                            {QStringLiteral("replacement"), QString()}}),
                 QStringLiteral("incomplete"));

        const QString rules = QStringLiteral("recognitionRules");
        QCOMPARE(library.add(rules, {{QStringLiteral("match"), QStringLiteral("obsidian")}}), QStringLiteral("incomplete"));
        QCOMPARE(library.add(rules, {{QStringLiteral("match"), QStringLiteral("obsidian")},
                                     {QStringLiteral("profile"), QStringLiteral("custom_profile_gone")}}),
                 QStringLiteral("missing"));
        QCOMPARE(library.add(QStringLiteral("terms"), {{QStringLiteral("term"), QStringLiteral(" ")}}), QStringLiteral("blank"));

        // A field no record has is the page's mistake, not the person's.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^Library\\.update\\(terms\\)")));
        QCOMPARE(library.update(QStringLiteral("terms"), QStringLiteral("Wayland"), {{QStringLiteral("uses"), 99}}),
                 QStringLiteral("invalid"));
        QCOMPARE(SettingsStore().dictationSnapshot().refinement.writingProfiles.size(), 6);
        QCOMPARE(SettingsStore().dictationSnapshot().bindings.size(), 1);
        QVERIFY(SettingsStore().dictationSnapshot().appRecognitionRules.isEmpty());
    }

    void removingAToneMovesItsProfilesUntilUndone()
    {
        ApplicationController controller(true);
        QuickLibrary library(&controller);
        const QString tone = library.newId(QStringLiteral("custom_tone"));
        QCOMPARE(library.add(QStringLiteral("customTones"), {{QStringLiteral("id"), tone},
                                                             {QStringLiteral("name"), QStringLiteral("Dry")},
                                                             {QStringLiteral("instruction"), QStringLiteral("Deadpan.")}}),
                 saved);
        QCOMPARE(library.update(QStringLiteral("profiles"), WritingProfile::Work, {{QStringLiteral("tone"), tone}}), saved);
        QCOMPARE(toneOf(WritingProfile::Work), tone);

        const int undo = library.removeTone(tone);
        QVERIFY(undo >= 0);
        QVERIFY(SettingsStore().dictationSnapshot().refinement.customTones.isEmpty());
        QCOMPARE(toneOf(WritingProfile::Work), QStringLiteral("none"));

        library.undo(undo);
        QCOMPARE(SettingsStore().dictationSnapshot().refinement.customTones.size(), 1);
        QCOMPARE(toneOf(WritingProfile::Work), tone);
        // A token works once.
        library.undo(undo);
        QCOMPARE(SettingsStore().dictationSnapshot().refinement.customTones.size(), 1);
        QCOMPARE(library.removeTone(QStringLiteral("custom_tone_gone")), -1);
    }

    // A custom tone or level can't go by a built-in one's name either: a
    // profile's choices would show two by that name.
    void customChoicesCantTakeABuiltInName()
    {
        ApplicationController controller(true);
        QuickLibrary library(&controller);
        const QStringList builtInTones{QStringLiteral("No tone"), QStringLiteral("Formal")};
        const QString tones = QStringLiteral("customTones");
        const QString tone = library.newId(QStringLiteral("custom_tone"));
        QCOMPARE(library.add(tones, {{QStringLiteral("id"), tone}, {QStringLiteral("name"), QStringLiteral(" formal ")},
                                     {QStringLiteral("instruction"), QStringLiteral("Stiff.")}},
                             builtInTones),
                 QStringLiteral("taken"));
        QCOMPARE(library.add(tones, {{QStringLiteral("id"), tone}, {QStringLiteral("name"), QStringLiteral("Dry")},
                                     {QStringLiteral("instruction"), QStringLiteral("Deadpan.")}},
                             builtInTones),
                 saved);
        QCOMPARE(library.update(tones, tone, {{QStringLiteral("name"), QStringLiteral("no tone")}}, builtInTones),
                 QStringLiteral("taken"));

        const QString levels = QStringLiteral("customCleanupLevels");
        const QString level = library.newId(QStringLiteral("custom_level"));
        QCOMPARE(library.add(levels, {{QStringLiteral("id"), level}, {QStringLiteral("name"), QStringLiteral("Notes")},
                                      {QStringLiteral("base"), QStringLiteral("balanced")}},
                             {QStringLiteral("Medium")}),
                 saved);
        QCOMPARE(library.update(levels, level, {{QStringLiteral("name"), QStringLiteral("MEDIUM")}}, {QStringLiteral("Medium")}),
                 QStringLiteral("taken"));
        QCOMPARE(SettingsStore().dictationSnapshot().refinement.customTones.first().name, QStringLiteral("Dry"));
        QCOMPARE(SettingsStore().dictationSnapshot().refinement.customCleanupLevels.first().name, QStringLiteral("Notes"));
    }

    // Undoing a tone's removal after another tone took its name keeps the
    // new one alone, and leaves the profiles on the tone they have.
    void undoingAToneLeavesOneByItsNameAlone()
    {
        ApplicationController controller(true);
        QuickLibrary library(&controller);
        const QString tones = QStringLiteral("customTones");
        const QString old = library.newId(QStringLiteral("custom_tone"));
        QCOMPARE(library.add(tones, {{QStringLiteral("id"), old}, {QStringLiteral("name"), QStringLiteral("Dry")},
                                     {QStringLiteral("instruction"), QStringLiteral("Deadpan.")}}),
                 saved);
        QCOMPARE(library.update(QStringLiteral("profiles"), WritingProfile::Work, {{QStringLiteral("tone"), old}}), saved);
        const int undo = library.removeTone(old);
        const QString again = library.newId(QStringLiteral("custom_tone"));
        QCOMPARE(library.add(tones, {{QStringLiteral("id"), again}, {QStringLiteral("name"), QStringLiteral("dry")},
                                     {QStringLiteral("instruction"), QStringLiteral("Flat.")}}),
                 saved);

        library.undo(undo);
        QCOMPARE(SettingsStore().dictationSnapshot().refinement.customTones.size(), 1);
        QCOMPARE(SettingsStore().dictationSnapshot().refinement.customTones.first().id, again);
        QCOMPARE(toneOf(WritingProfile::Work), QStringLiteral("none"));
    }

    void removingAProfileResetsTheFallbackSetting()
    {
        ApplicationController controller(true);
        QuickLibrary library(&controller);
        QuickSettings settings(&controller);
        const QString id = library.newId(QStringLiteral("custom_profile"));
        QCOMPARE(library.add(QStringLiteral("profiles"), profileItem(id, QStringLiteral("Notes"))), saved);
        settings.set(QStringLiteral("defaultWritingProfile"), id);
        QCOMPARE(library.add(QStringLiteral("recognitionRules"),
                             {{QStringLiteral("match"), QStringLiteral("obsidian")}, {QStringLiteral("profile"), id}}),
                 saved);
        QCOMPARE(SettingsStore().dictationSnapshot().refinement.defaultWritingProfile, id);

        library.removeProfile(id);
        QCOMPARE(SettingsStore().dictationSnapshot().refinement.defaultWritingProfile, WritingProfile::Other);
        QCOMPARE(settings.values().value(QStringLiteral("defaultWritingProfile")).toString(), WritingProfile::Other);
        QVERIFY(SettingsStore().dictationSnapshot().appRecognitionRules.isEmpty());
        QCOMPARE(library.profiles().size(), 5);
    }

    // Transcribe keeps a profile, tone or level only while the Library
    // offers it: deleting one here gives way to the profile's own there, as
    // it would for dictation.
    void transcribeGivesUpWhatTheLibraryDeletes()
    {
        ApplicationController controller(true);
        QuickLibrary library(&controller);
        QuickTranscription transcription(&controller);
        const QString tone = library.newId(QStringLiteral("custom_tone"));
        QCOMPARE(library.add(QStringLiteral("customTones"), {{QStringLiteral("id"), tone},
                                                             {QStringLiteral("name"), QStringLiteral("Dry")},
                                                             {QStringLiteral("instruction"), QStringLiteral("Deadpan.")}}),
                 saved);
        const QString level = library.newId(QStringLiteral("custom_level"));
        QCOMPARE(library.add(QStringLiteral("customCleanupLevels"), {{QStringLiteral("id"), level},
                                                                     {QStringLiteral("name"), QStringLiteral("Terse")},
                                                                     {QStringLiteral("base"), QStringLiteral("light_cleanup")}}),
                 saved);
        const QString profile = library.newId(QStringLiteral("custom_profile"));
        QCOMPARE(library.add(QStringLiteral("profiles"), profileItem(profile, QStringLiteral("Notes"))), saved);
        transcription.setOption(QStringLiteral("profile"), profile);
        transcription.setOption(QStringLiteral("tone"), tone);
        transcription.setOption(QStringLiteral("cleanup"), level);
        const auto option = [&transcription](const char *id) {
            return transcription.options().value(QLatin1String(id)).toString();
        };
        QCOMPARE(option("tone"), tone);
        QCOMPARE(option("cleanup"), level);

        QVERIFY(library.removeTone(tone) >= 0);
        QCOMPARE(option("tone"), QStringLiteral("none"));
        QCOMPARE(option("cleanup"), level);
        QVERIFY(library.removeLevel(level) >= 0);
        QCOMPARE(option("cleanup"), QStringLiteral("balanced"));

        library.removeProfile(profile);
        QCOMPARE(option("profile"), WritingProfile::Other);
    }

    void editingATermKeepsWhatTheListDoesNotShow()
    {
        {
            SettingsStore store;
            VocabularyEntry learned{QStringLiteral("Ngozi"), QStringLiteral("learned")};
            learned.frequency = 17;
            learned.lastUsedMs = 1700000000000;
            store.edit([&](AppSettings &draft) { draft.vocabulary = {learned}; return true; });
        }
        ApplicationController controller(true);
        QuickLibrary library(&controller);
        QCOMPARE(library.terms().first().toMap().value(QStringLiteral("uses")).toInt(), 17);

        QCOMPARE(library.update(QStringLiteral("terms"), QStringLiteral("ngozi"),
                                {{QStringLiteral("keyTerm"), false}, {QStringLiteral("context"), QStringLiteral("A name.")},
                                 {QStringLiteral("profiles"), QStringList{QStringLiteral("work"), QStringLiteral("gone")}}}),
                 saved);
        const VocabularyEntry stored = SettingsStore().dictationSnapshot().vocabulary.first();
        QCOMPARE(stored.frequency, 17);
        QCOMPARE(stored.lastUsedMs, qint64(1700000000000));
        QCOMPARE(stored.source, QStringLiteral("learned"));
        QVERIFY(!stored.keyTerm);
        QCOMPARE(stored.context, QStringLiteral("A name."));
        QCOMPARE(stored.profiles, QStringList{QStringLiteral("work")});
        QCOMPARE(library.add(QStringLiteral("terms"), {{QStringLiteral("term"), QStringLiteral(" ngozi ")}}), QStringLiteral("taken"));

        const int undo = library.remove(QStringLiteral("terms"), QStringLiteral("Ngozi"));
        QVERIFY(SettingsStore().dictationSnapshot().vocabulary.isEmpty());
        QCOMPARE(library.remove(QStringLiteral("terms"), QStringLiteral("Ngozi")), -1);
        library.undo(undo);
        QCOMPARE(SettingsStore().dictationSnapshot().vocabulary.first().frequency, 17);
    }

    // A form names the term it opened on. Dictation counting a use reorders
    // the list meanwhile, and the save still changes that term alone.
    void anEditAfterDictationReordersChangesOnlyItsTerm()
    {
        SettingsStore().edit([](AppSettings &draft) {
            draft.vocabulary = {termEntry(QStringLiteral("Alpha")), termEntry(QStringLiteral("Zulu"))};
            return true;
        });
        ApplicationController controller(true);
        QuickLibrary library(&controller);
        QCOMPARE(library.terms().first().toMap().value(QStringLiteral("term")).toString(), QStringLiteral("Alpha"));

        QVERIFY(controller.settings()->recordVocabularyUsage(QStringLiteral("ship it to Zulu")));
        QCOMPARE(library.terms().first().toMap().value(QStringLiteral("term")).toString(), QStringLiteral("Zulu"));

        QCOMPARE(library.update(QStringLiteral("terms"), QStringLiteral("Alpha"),
                                {{QStringLiteral("term"), QStringLiteral("Alpha")}, {QStringLiteral("context"), QStringLiteral("The first.")}}),
                 saved);
        const QList<VocabularyEntry> stored = SettingsStore().dictationSnapshot().vocabulary;
        QCOMPARE(stored.size(), 2);
        QCOMPARE(findTerm(stored, QStringLiteral("Alpha"))->context, QStringLiteral("The first."));
        QVERIFY(findTerm(stored, QStringLiteral("Zulu")));
        QCOMPARE(findTerm(stored, QStringLiteral("Zulu"))->context, QString());
        QCOMPARE(findTerm(stored, QStringLiteral("Zulu"))->frequency, 1);

        // One deleted while its form was open is gone, not another's.
        QVERIFY(library.remove(QStringLiteral("terms"), QStringLiteral("Alpha")) >= 0);
        QCOMPARE(library.update(QStringLiteral("terms"), QStringLiteral("Alpha"), {{QStringLiteral("context"), QStringLiteral("x")}}),
                 QStringLiteral("missing"));
        QCOMPARE(SettingsStore().dictationSnapshot().vocabulary.first().context, QString());
    }

    // Undo puts back what was deleted only while nothing by its name has
    // come back: a term added again keeps its new values, and a correction
    // learned again is not listed twice.
    void undoKeepsWhatCameBackMeanwhile()
    {
        SettingsStore().edit([](AppSettings &draft) {
            draft.vocabulary = {termEntry(QStringLiteral("Grafana"), QStringLiteral("Old."))};
            return true;
        });
        ApplicationController controller(true);
        QuickLibrary library(&controller);

        const int termUndo = library.remove(QStringLiteral("terms"), QStringLiteral("Grafana"));
        QCOMPARE(library.add(QStringLiteral("terms"),
                             {{QStringLiteral("term"), QStringLiteral("grafana")}, {QStringLiteral("context"), QStringLiteral("New.")}}),
                 saved);
        library.undo(termUndo);
        QCOMPARE(SettingsStore().dictationSnapshot().vocabulary.size(), 1);
        QCOMPARE(SettingsStore().dictationSnapshot().vocabulary.first().context, QStringLiteral("New."));

        const CorrectionEvidence evidence{QStringLiteral("speaker"), QStringLiteral("Speecher"), 0.95};
        QVERIFY(controller.settings()->recordCorrectionEvidence(evidence, QStringLiteral("slack")));
        const int correctionUndo = library.undoLastCorrection();
        QVERIFY(correctionUndo >= 0);
        QVERIFY(controller.settings()->recordCorrectionEvidence(evidence, QStringLiteral("slack")));
        library.undo(correctionUndo);
        QCOMPARE(SettingsStore().dictationSnapshot().learnedCorrections.size(), 1);
    }

    // Only the newest removal can be undone, as the window's one toast offers.
    void aNewerRemovalRetiresTheOlderUndo()
    {
        SettingsStore().edit([](AppSettings &draft) {
            draft.vocabulary = {termEntry(QStringLiteral("Grafana")), termEntry(QStringLiteral("Kafka"))};
            return true;
        });
        ApplicationController controller(true);
        QuickLibrary library(&controller);

        const int older = library.remove(QStringLiteral("terms"), QStringLiteral("Grafana"));
        const int newer = library.remove(QStringLiteral("terms"), QStringLiteral("Kafka"));
        library.undo(older);
        QVERIFY(SettingsStore().dictationSnapshot().vocabulary.isEmpty());
        library.undo(newer);
        QCOMPARE(SettingsStore().dictationSnapshot().vocabulary.size(), 1);
        QCOMPARE(SettingsStore().dictationSnapshot().vocabulary.first().term, QStringLiteral("Kafka"));
    }

    void appMatchesTakeOverTheOldPerAppOverrides()
    {
        SettingsStore().edit([](AppSettings &draft) {
            draft.refinement.writingProfileOverrides = {{QStringLiteral("org.kde.kate"), WritingProfile::Email, true}};
            return true;
        });
        ApplicationController controller(true);
        QuickLibrary library(&controller);
        QCOMPARE(library.recognitionRules().size(), 1);
        QCOMPARE(library.recognitionRules().first().toMap().value(QStringLiteral("profile")).toString(), WritingProfile::Email);
        QVERIFY(!library.builtInRecognitionRules().isEmpty());

        QCOMPARE(library.add(QStringLiteral("recognitionRules"),
                             {{QStringLiteral("match"), QStringLiteral("konsole")}, {QStringLiteral("category"), QStringLiteral("terminal")}}),
                 saved);
        SettingsStore another;
        QVERIFY(another.dictationSnapshot().refinement.writingProfileOverrides.isEmpty());
        QCOMPARE(another.dictationSnapshot().appRecognitionRules.size(), 2);
        QCOMPARE(another.dictationSnapshot().appRecognitionRules.last().category, AppCategory::Terminal);

        QCOMPARE(library.add(QStringLiteral("recognitionRules"), {{QStringLiteral("match"), QStringLiteral("Konsole")},
                                                                   {QStringLiteral("profile"), WritingProfile::Work}}),
                 QStringLiteral("taken"));
        QCOMPARE(library.update(QStringLiteral("recognitionRules"), QStringLiteral("konsole"), {{QStringLiteral("category"), QString()}}),
                 QStringLiteral("incomplete"));
    }

    // Renaming a match keeps its place and what it sets.
    void anAppMatchCanBeRenamedInPlace()
    {
        SettingsStore().edit([](AppSettings &draft) {
            draft.appRecognitionRules = {{QStringLiteral("obsidian"), AppCategory::Office, WritingProfile::Personal},
                                                    {QStringLiteral("neochat"), std::nullopt, WritingProfile::Personal}};
            return true;
        });
        ApplicationController controller(true);
        QuickLibrary library(&controller);

        QCOMPARE(library.update(QStringLiteral("recognitionRules"), QStringLiteral("Obsidian"),
                                {{QStringLiteral("match"), QStringLiteral(" md.obsidian ")}}),
                 saved);
        const QList<AppRecognitionRule> rules = SettingsStore().dictationSnapshot().appRecognitionRules;
        QCOMPARE(rules.size(), 2);
        QCOMPARE(rules.first().match, QStringLiteral("md.obsidian"));
        QCOMPARE(rules.first().category, AppCategory::Office);
        QCOMPARE(rules.first().writingProfile, WritingProfile::Personal);
        QCOMPARE(library.update(QStringLiteral("recognitionRules"), QStringLiteral("md.obsidian"),
                                {{QStringLiteral("match"), QStringLiteral("NeoChat")}}),
                 QStringLiteral("taken"));
    }

    // CSV terms merge into the list as the old editor merged them: a term
    // listed already keeps its own settings, takes a context it lacked and
    // the higher use count.
    void importsMergeTermsAndSayWhyAFileFailed()
    {
        QTemporaryDir dir;
        {
            VocabularyEntry grafana = termEntry(QStringLiteral("Grafana"));
            grafana.keyTerm = false;
            grafana.frequency = 2;
            SettingsStore().edit([&](AppSettings &draft) { draft.vocabulary = {grafana}; return true; });
        }
        ApplicationController controller(true);
        QuickLibrary library(&controller);

        const QUrl csv = QUrl::fromLocalFile(writeFile(dir, "terms.csv",
                                                       "term,frequency,context\n"
                                                       "grafana,9,Our dashboards\n"
                                                       "Priya Raman,0,\n"));
        const QVariantMap terms = library.importTerms(csv);
        QCOMPARE(terms.value(QStringLiteral("added")).toInt(), 1);
        QCOMPARE(terms.value(QStringLiteral("error")).toString(), QString());
        const QList<VocabularyEntry> stored = SettingsStore().dictationSnapshot().vocabulary;
        QCOMPARE(stored.size(), 2);
        const VocabularyEntry *grafana = findTerm(stored, QStringLiteral("Grafana"));
        QVERIFY(grafana);
        QVERIFY(!grafana->keyTerm);
        QCOMPARE(grafana->context, QStringLiteral("Our dashboards"));
        QCOMPARE(grafana->frequency, 9);
        QCOMPARE(findTerm(stored, QStringLiteral("Priya Raman"))->source, QStringLiteral("csv"));
        // The same file again changes nothing, so nothing is saved.
        QSignalSpy saves(controller.settings(), &SettingsStore::snapshotApplied);
        QCOMPARE(library.importTerms(csv).value(QStringLiteral("added")).toInt(), 0);
        QCOMPARE(saves.count(), 0);

        const QVariantMap broken = library.importTerms(QUrl::fromLocalFile(writeFile(dir, "broken.csv", "term\n\"Unclosed\n")));
        QVERIFY(!broken.value(QStringLiteral("error")).toString().isEmpty());
        QVERIFY(!library.importTerms(QUrl::fromLocalFile(dir.filePath(QStringLiteral("missing.csv"))))
                     .value(QStringLiteral("error")).toString().isEmpty());

        const QUrl json = QUrl::fromLocalFile(writeFile(dir, "snippets.json", R"([{"phrase": "sign off", "replacement": "Thanks"}])"));
        QCOMPARE(library.importReplacements(json).value(QStringLiteral("added")).toInt(), 1);
        QCOMPARE(library.importReplacements(json).value(QStringLiteral("added")).toInt(), 0);
        QCOMPARE(SettingsStore().dictationSnapshot().bindings.size(), 1);
    }

    void learnedCorrectionsShowAsTheyAreLearnedAndCanBeTakenBack()
    {
        ApplicationController controller(true);
        QuickLibrary library(&controller);
        QSignalSpy changed(&library, &QuickLibrary::listsChanged);
        QVERIFY(controller.settings()->recordCorrectionEvidence({QStringLiteral("speaker"), QStringLiteral("Speecher"), 0.95},
                                                                QStringLiteral("slack")));
        QCOMPARE(changed.count(), 1);
        const QString id = library.corrections().first().toMap().value(QStringLiteral("id")).toString();
        QCOMPARE(library.corrections().first().toMap().value(QStringLiteral("corrected")).toString(), QStringLiteral("Speecher"));

        QCOMPARE(library.update(QStringLiteral("corrections"), id, {{QStringLiteral("enabled"), false}}), saved);
        QVERIFY(!SettingsStore().dictationSnapshot().learnedCorrections.first().enabled);

        const int undo = library.undoLastCorrection();
        QVERIFY(SettingsStore().dictationSnapshot().learnedCorrections.isEmpty());
        QCOMPARE(library.undoLastCorrection(), -1);
        library.undo(undo);
        QCOMPARE(SettingsStore().dictationSnapshot().learnedCorrections.first().original, QStringLiteral("speaker"));
    }

    // A correction's words can be fixed by hand, keeping what it was learned
    // from, and any one of them deleted with an Undo.
    void aCorrectionCanBeEditedAndDeleted()
    {
        LearnedCorrection learned{QStringLiteral("c1"), QStringLiteral("cube control"), QStringLiteral("kubectl"),
                                  QStringLiteral("org.kde.konsole"), 1700000000000, 0.88, true, 3, 1700000001000};
        LearnedCorrection other{QStringLiteral("c2"), QStringLiteral("get hub"), QStringLiteral("GitHub"),
                                QStringLiteral("org.kde.konsole"), 1700000000000, 0.7, true, 2, 1700000002000};
        SettingsStore().edit([&](AppSettings &draft) { draft.learnedCorrections = {other, learned}; return true; });
        ApplicationController controller(true);
        QuickLibrary library(&controller);

        QCOMPARE(library.update(QStringLiteral("corrections"), QStringLiteral("c1"),
                                {{QStringLiteral("heard"), QStringLiteral(" cube CTL ")}, {QStringLiteral("corrected"), QStringLiteral("kubectl")}}),
                 saved);
        const LearnedCorrection edited = SettingsStore().dictationSnapshot().learnedCorrections.last();
        QCOMPARE(edited.original, QStringLiteral("cube CTL"));
        QCOMPARE(edited.evidenceCount, 3);
        QCOMPARE(edited.confidence, 0.88);
        QCOMPARE(edited.applicationId, QStringLiteral("org.kde.konsole"));
        QCOMPARE(library.update(QStringLiteral("corrections"), QStringLiteral("c1"), {{QStringLiteral("heard"), QStringLiteral("Get Hub")}}),
                 QStringLiteral("taken"));
        QCOMPARE(library.update(QStringLiteral("corrections"), QStringLiteral("c1"), {{QStringLiteral("corrected"), QString()}}),
                 QStringLiteral("incomplete"));

        const int undo = library.remove(QStringLiteral("corrections"), QStringLiteral("c1"));
        QCOMPARE(SettingsStore().dictationSnapshot().learnedCorrections.size(), 1);
        QCOMPARE(SettingsStore().dictationSnapshot().learnedCorrections.first().id, QStringLiteral("c2"));
        library.undo(undo);
        QCOMPARE(SettingsStore().dictationSnapshot().learnedCorrections.last(), edited);
    }

    // A choice in a match's row that core refuses, such as clearing the
    // only thing a match sets, is said in a toast and saves nothing.
    void anAppMatchRowSaysWhyAChoiceWasRefused()
    {
        SettingsStore().edit([](AppSettings &draft) {
            draft.appRecognitionRules = {{QStringLiteral("obsidian"), AppCategory::Office, std::nullopt}};
            return true;
        });
        ApplicationController controller(true);
        const QuickLibrary::ControllerScope scope(&controller);
        QuickSettings settings(&controller);
        // Library's key-term line names Models' speech service, and the host
        // always has Models.
        QuickModels models(&controller);
        QuickNavigation navigation(nullptr);
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/Speecher/App/pages/text/AppRecognitionRules.qml")));
        const std::unique_ptr<QObject> section(component.create());
        QVERIFY2(section, qPrintable(component.errorString()));
        // A row is a ListView delegate: an item child, not a QObject one.
        QObject *typeSelect = nullptr;
        QList<QQuickItem *> items{qobject_cast<QQuickItem *>(section.get())};
        while (!items.isEmpty()) {
            QQuickItem *item = items.takeLast();
            items += item->childItems();
            if (QByteArray(item->metaObject()->className()).startsWith("Select_")
                && item->property("currentId").toString() == QLatin1String("office")) {
                typeSelect = item;
            }
        }
        QVERIFY(typeSelect);
        QObject *toaster = engine.singletonInstance<QObject *>("Speecher.Controls", "Toaster");
        QSignalSpy posted(toaster, SIGNAL(posted(QVariant)));

        QMetaObject::invokeMethod(typeSelect, "chosen", Q_ARG(QString, QString()));
        QCOMPARE(posted.count(), 1);
        QCOMPARE(posted.first().first().toMap().value(QStringLiteral("text")).toString(),
                 QStringLiteral("Choose an app type, a profile or both."));
        QCOMPARE(SettingsStore().dictationSnapshot().appRecognitionRules.first().category, std::optional(AppCategory::Office));
    }

    // The app's Library is Library.qml over this type: its wording reads the
    // real lists.
    void libraryInTheUiReadsTheRealLists()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        const QuickLibrary::ControllerScope scope(&controller);
        // Library's key-term line names Models' speech service, and the host
        // always has Models.
        QuickModels models(&controller);
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

        QVariant refused;
        QMetaObject::invokeMethod(probe.get(), "addTerm", Q_RETURN_ARG(QVariant, refused));
        QCOMPARE(refused.toString(), saved);
        QCOMPARE(SettingsStore().dictationSnapshot().vocabulary.first().term, QStringLiteral("Wayland"));
    }
};

int runQuickLibraryTests(int argc, char **argv)
{
    QuickLibraryTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_quick_library.moc"
