// The UI's History singleton over the controller's History: searching as
// the person types, matches that never become markup, delete with Undo,
// filters and pages.

#include "common/test_suites.h"
#include "common/locked_folder.h"
#include "common/test_doubles.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "core/history/HistoryStore.h"
#include "frontend/quick/host/QuickHistory.h"
#include "providers/ProviderRegistry.h"

#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QDir>
#include <QTest>

using namespace speecher;

namespace {

HistoryEntry kept(const QString &text, HistoryKind kind = HistoryKind::Dictation, int minutesAgo = 0)
{
    HistoryEntry entry;
    entry.kind = kind;
    entry.stats.finishedAt = QDateTime::currentDateTime().addSecs(-60 * minutesAgo);
    entry.stats.appName = QStringLiteral("Slack");
    entry.kept = HistoryKeep::Text;
    entry.text = text;
    entry.rawText = text;
    return entry;
}

QStringList textsOf(const QVariantList &results)
{
    QStringList texts;
    for (const QVariant &result : results) {
        texts.append(result.toMap().value(QStringLiteral("text")).toString());
    }
    return texts;
}

} // namespace

class QuickHistoryTests : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        ApplicationController controller(true);
        controller.history()->clear({}, {}, true);
    }

    // Typing searches once it pauses: the queries in between never run, and
    // the results are the last query's.
    void searchWaitsForTypingToPause()
    {
        ApplicationController controller(true);
        controller.history()->add(kept(QStringLiteral("send the deck"), HistoryKind::Dictation, 2));
        controller.history()->add(kept(QStringLiteral("lunch at noon"), HistoryKind::Dictation, 1));
        QuickHistory history(&controller);
        QCOMPARE(history.matches(), 2);
        QSignalSpy results(&history, &QuickHistory::resultsChanged);

        history.setQuery({{QStringLiteral("text"), QStringLiteral("lun")}});
        history.setQuery({{QStringLiteral("text"), QStringLiteral("dec")}});
        history.setQuery({{QStringLiteral("text"), QStringLiteral("deck")}});
        QVERIFY(history.searching());
        QCOMPARE(results.count(), 0);
        QTRY_VERIFY(!history.searching());
        QCOMPARE(results.count(), 1);
        QCOMPARE(textsOf(history.results()), QStringList{QStringLiteral("send the deck")});
        QCOMPARE(history.matches(), 1);
    }

    // What was dictated shows as typed, markup and all; only the matches
    // are bold.
    void matchesAreBoldAndTheRestIsEscaped()
    {
        ApplicationController controller(true);
        controller.history()->add(kept(QStringLiteral("Use <b>bold</b> & <i>tags</i>\nnext line")));
        QuickHistory history(&controller);
        history.setQuery({{QStringLiteral("text"), QStringLiteral("bold")}});
        QTRY_VERIFY(!history.searching());
        QCOMPARE(history.results().size(), 1);
        QCOMPARE(history.results().first().toMap().value(QStringLiteral("html")).toString(),
                 QStringLiteral("Use &lt;b&gt;<b>bold</b>&lt;/b&gt; &amp; &lt;i&gt;tags&lt;/i&gt;<br>next line"));
    }

    // A delete hides the entry at once; Undo brings it back until it is
    // purged for good.
    void aDeleteCanBeUndone()
    {
        ApplicationController controller(true);
        const qint64 id = controller.history()->add(kept(QStringLiteral("delete me")));
        QuickHistory history(&controller);
        QCOMPARE(history.kept(), 1);

        const int token = history.remove(id);
        QVERIFY(token >= 0);
        QVERIFY(history.results().isEmpty());
        QCOMPARE(history.kept(), 0);
        QVERIFY(history.undo(token));
        QCOMPARE(textsOf(history.results()), QStringList{QStringLiteral("delete me")});

        const int again = history.remove(id);
        controller.history()->purgeDeleted();
        QVERIFY(!history.undo(again));
        QCOMPARE(history.remove(id), -1);
    }

    // The kind and the day narrow the list; the filters list the apps History
    // holds.
    void filtersNarrowTheList()
    {
        ApplicationController controller(true);
        controller.history()->add(kept(QStringLiteral("an old note"), HistoryKind::Dictation, 3 * 24 * 60));
        HistoryEntry file = kept(QStringLiteral("the interview"), HistoryKind::File, 1);
        file.title = QStringLiteral("interview.m4a");
        file.stats.appName.clear();
        controller.history()->add(file);
        controller.history()->add(kept(QStringLiteral("today's note")));
        QuickHistory history(&controller);
        QCOMPARE(history.apps(), QStringList{QStringLiteral("Slack")});

        history.setQuery({{QStringLiteral("kind"), QStringLiteral("file")}});
        QTRY_VERIFY(!history.searching());
        QCOMPARE(textsOf(history.results()), QStringList{QStringLiteral("the interview")});
        history.setQuery({{QStringLiteral("kind"), QStringLiteral("dictation")}, {QStringLiteral("when"), QStringLiteral("today")}});
        QTRY_VERIFY(!history.searching());
        QCOMPARE(textsOf(history.results()), QStringList{QStringLiteral("today's note")});
    }

    // Renaming a profile renames it in the filter's choices and the rows.
    void aRenamedProfileShowsItsNewName()
    {
        ApplicationController controller(true);
        controller.settings()->edit([](AppSettings &draft) {
            draft.refinement.writingProfiles.append({QStringLiteral("custom_notes"), QStringLiteral("balanced"),
                                                     QStringLiteral("none"), QString(), QStringLiteral("Notes")});
            return true;
        });
        HistoryEntry note = kept(QStringLiteral("buy milk"));
        note.stats.profile = QStringLiteral("custom_notes");
        controller.history()->add(note);
        QuickHistory history(&controller);
        const auto names = [&history] {
            return std::pair{history.profiles().first().toMap().value(QStringLiteral("name")).toString(),
                             history.results().first().toMap().value(QStringLiteral("profileName")).toString()};
        };
        QCOMPARE(names(), std::pair(QStringLiteral("Notes"), QStringLiteral("Notes")));
        QSignalSpy changed(&history, &QuickHistory::changed);

        controller.settings()->edit([](AppSettings &draft) {
            draft.refinement.writingProfiles.last().name = QStringLiteral("Journal");
            return true;
        });
        QCOMPARE(names(), std::pair(QStringLiteral("Journal"), QStringLiteral("Journal")));
        QCOMPARE(changed.count(), 1);
    }

    // A page at a time, and a new entry comes in at the top of what shows.
    void resultsComeAPageAtATime()
    {
        ApplicationController controller(true);
        QList<HistoryEntry> entries;
        for (int i = 0; i < QuickHistory::pageSize + 20; ++i) {
            entries.append(kept(QStringLiteral("note %1").arg(i), HistoryKind::Dictation, 1000 - i));
        }
        QVERIFY(controller.history()->addAll(entries));
        QuickHistory history(&controller);
        QCOMPARE(history.results().size(), QuickHistory::pageSize);
        QVERIFY(history.hasMore());
        history.fetchMore();
        QCOMPARE(history.results().size(), QuickHistory::pageSize + 20);
        QVERIFY(!history.hasMore());

        // As many as showed, the newest first; the oldest waits for the next page.
        controller.history()->add(kept(QStringLiteral("the newest")));
        QCOMPARE(history.results().size(), QuickHistory::pageSize + 20);
        QCOMPARE(history.results().first().toMap().value(QStringLiteral("text")).toString(), QStringLiteral("the newest"));
        QVERIFY(history.hasMore());
    }

    // Clear asks how many first, and deletes the words; with the stats, the
    // entries too.
    // A Recording lists its lines that match the search; in full it has all
    // of them. Export writes them as its file has them or
    // as JSON, and a delete can take its files with it.
    void aRecordingShowsItsLinesExportsAndGoes()
    {
        ApplicationController controller(true);
        QTemporaryDir dir;
        QFile transcript(dir.filePath(QStringLiteral("weekly-sync.md")));
        QVERIFY(transcript.open(QIODevice::WriteOnly));
        transcript.close();
        HistoryEntry recording;
        recording.kind = HistoryKind::Recording;
        recording.startedAt = QDateTime::currentDateTime();
        recording.title = QStringLiteral("Weekly sync");
        recording.transcriptPath = transcript.fileName();
        recording.kept = HistoryKeep::Text;
        recording.outcome = HistoryOutcome::Complete;
        HistoryStore *store = controller.history();
        const qint64 id = store->add(recording);
        store->appendLine(id, {4000, QStringLiteral("me"), QStringLiteral("Can you see <my> screen?")});
        store->appendLine(id, {752000, QStringLiteral("them"), QStringLiteral("It retries after a 403")});
        store->finishRecording(id, QDateTime::currentDateTime(), 760000, {}, {});
        QuickHistory history(&controller);
        history.setQuery({{QStringLiteral("text"), QStringLiteral("403")}, {QStringLiteral("kind"), QStringLiteral("recording")}});
        QTRY_VERIFY(!history.searching());
        QCOMPARE(history.results().size(), 1);
        const QVariantMap found = history.results().first().toMap();
        QCOMPARE(found.value(QStringLiteral("kind")).toString(), QStringLiteral("recording"));
        QVERIFY(!found.value(QStringLiteral("live")).toBool());
        const QVariantList lines = found.value(QStringLiteral("lines")).toList();
        QCOMPARE(lines.size(), 1);
        QCOMPARE(lines.first().toMap().value(QStringLiteral("text")).toString(), QStringLiteral("It retries after a 403"));
        QCOMPARE(history.entry(id).value(QStringLiteral("lines")).toList().size(), 2);

        const QUrl markdown = history.exportFile(id, QStringLiteral("txt"));
        QCOMPARE(markdown.toLocalFile(), dir.filePath(QStringLiteral("weekly-sync.txt")));
        QVERIFY(history.exportEntry(id, QStringLiteral("txt"), markdown));
        QFile exported(markdown.toLocalFile());
        QVERIFY(exported.open(QIODevice::ReadOnly));
        QCOMPARE(exported.readAll(), QByteArray("[00:00:04] me: Can you see <my> screen?\n"
                                                "[00:12:32] them: It retries after a 403\n"));
        const QUrl json = history.exportFile(id, QStringLiteral("json"));
        QVERIFY(history.exportEntry(id, QStringLiteral("json"), json));
        QFile jsonFile(json.toLocalFile());
        QVERIFY(jsonFile.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(jsonFile.readAll()).array().at(1).toObject(),
                 (QJsonObject{{QStringLiteral("atMs"), 752000},
                              {QStringLiteral("speaker"), QStringLiteral("them")},
                              {QStringLiteral("text"), QStringLiteral("It retries after a 403")}}));

        // A file that won't go keeps the entry, and says which it is.
        {
            test::LockedFolder locked(dir.path());
            if (locked.holds()) {
                QCOMPARE(history.removeRecording(id, true), QStringList{transcript.fileName()});
                QVERIFY(!history.entry(id).isEmpty());
            }
        }
        QVERIFY(history.removeRecording(id, true).isEmpty());
        QVERIFY(!QFile::exists(transcript.fileName()));
        QVERIFY(history.entry(id).isEmpty());
    }

    // Refine again refines the Raw Transcript with the profile chosen, and
    // with no app's context: no nearby text, no screenshot. Keep this keeps
    // the new text and the raw one.
    void refineAgainUsesTheChosenProfileAndNoAppsContext()
    {
        ApplicationController controller(true);
        QPointer<test::FakeRefiner> refiner;
        controller.providerRegistry()->registerRefinementProvider(
            {QStringLiteral("openai"), QStringLiteral("ChatGPT")}, [&refiner](QObject *parent) {
                refiner = new test::FakeRefiner(parent);
                refiner->autoComplete = true;
                refiner->autoCompleteText = QStringLiteral("Could you send the deck?");
                return refiner.data();
            });
        controller.settings()->edit([](AppSettings &draft) {
            draft.refinement.enabled = true;
            draft.refinement.services.order = {QStringLiteral("openai")};
            return true;
        });
        HistoryEntry spoken = kept(QStringLiteral("Send the deck."));
        spoken.rawText = QStringLiteral("um send the deck");
        spoken.stats.profile = WritingProfile::Work;
        const qint64 id = controller.history()->add(spoken);
        QuickHistory history(&controller);
        QSignalSpy refined(&history, &QuickHistory::refined);
        history.refineAgain(id, WritingProfile::Email);
        QTRY_COMPARE(refined.count(), 1);
        QCOMPARE(refined.first(), (QVariantList{id, QStringLiteral("Could you send the deck?")}));
        QCOMPARE(refiner->lastRawTranscript, QStringLiteral("um send the deck"));
        QCOMPARE(refiner->lastContext.writingProfile, WritingProfile::Email);
        QVERIFY(refiner->lastContext.target.applicationId.isEmpty());
        QVERIFY(refiner->lastContext.target.nearbyTextBefore.isEmpty());
        QVERIFY(refiner->lastContext.screenshotData.isEmpty());

        QVERIFY(history.keepRefined(id, QStringLiteral("Could you send the deck?")));
        QCOMPARE(controller.history()->entry(id)->text, std::optional(QStringLiteral("Could you send the deck?")));
        QCOMPARE(controller.history()->entry(id)->rawText, std::optional(QStringLiteral("um send the deck")));
    }

    // Asked for, Refine again refines with Clean up my words off too; with a
    // profile whose cleanup is None, it says why rather than handing the
    // words back as if refined.
    void refineAgainRunsWithCleanupOffAndSaysWhyItCant()
    {
        ApplicationController controller(true);
        controller.providerRegistry()->registerRefinementProvider(
            {QStringLiteral("openai"), QStringLiteral("ChatGPT")}, [](QObject *parent) {
                auto *refiner = new test::FakeRefiner(parent);
                refiner->autoComplete = true;
                refiner->autoCompleteText = QStringLiteral("Could you send the deck?");
                return refiner;
            });
        controller.settings()->edit([](AppSettings &draft) {
            draft.refinement.enabled = false;
            draft.refinement.services.order = {QStringLiteral("openai")};
            return true;
        });
        HistoryEntry spoken = kept(QStringLiteral("um send the deck"));
        spoken.rawText = QStringLiteral("um send the deck");
        const qint64 id = controller.history()->add(spoken);
        QuickHistory history(&controller);
        QSignalSpy refined(&history, &QuickHistory::refined);
        QSignalSpy failed(&history, &QuickHistory::refineFailed);
        history.refineAgain(id, WritingProfile::Email);
        QTRY_COMPARE(refined.count(), 1);
        QCOMPARE(refined.first().last().toString(), QStringLiteral("Could you send the deck?"));

        controller.settings()->edit([](AppSettings &draft) {
            for (WritingProfileSettings &profile : draft.refinement.writingProfiles) {
                if (profile.profile == WritingProfile::Email) {
                    profile.cleanupStrength = QStringLiteral("none");
                }
            }
            return true;
        });
        history.refineAgain(id, WritingProfile::Email);
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(!failed.first().last().toString().isEmpty());
        QCOMPARE(refined.count(), 1);
    }

    void clearDeletesASpan()
    {
        ApplicationController controller(true);
        controller.history()->add(kept(QStringLiteral("last week"), HistoryKind::Dictation, 8 * 24 * 60));
        controller.history()->add(kept(QStringLiteral("just now")));
        QuickHistory history(&controller);
        QCOMPARE(history.countIn(QStringLiteral("hour")), 1);
        QCOMPARE(history.countIn(QStringLiteral("all")), 2);
        QCOMPARE(history.clear(QStringLiteral("hour"), false),
                 (QVariantMap{{QStringLiteral("succeeded"), true},
                              {QStringLiteral("cleared"), 1}, {QStringLiteral("left"), 0}, {QStringLiteral("files"), QStringList()}}));
        QCOMPARE(textsOf(history.results()), QStringList{QStringLiteral("last week")});
        QCOMPARE(controller.history()->records().size(), 2);
        QCOMPARE(history.clear(QStringLiteral("all"), true).value(QStringLiteral("left")).toInt(), 0);
        QCOMPARE(history.kept(), 0);
        QVERIFY(controller.history()->records().isEmpty());
    }

    // A clear of the stats too that can't finish (its recent apps won't go)
    // says so, though no entry holding text stayed.
    void aClearOfStatsThatFailsSaysSo()
    {
        ApplicationController controller(true);
        HistoryEntry stats = kept(QStringLiteral("stats"));
        stats.kept = HistoryKeep::Stats;
        controller.history()->add(stats);
        const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        QFile seen(QDir(folder).filePath(QStringLiteral("seen-apps.json")));
        QVERIFY(seen.open(QIODevice::WriteOnly));
        seen.write("[]");
        seen.close();
        const auto cleanUp = qScopeGuard([&seen] { seen.remove(); });
        QuickHistory history(&controller);
        {
            test::LockedFolder locked(folder);
            if (!locked.holds()) {
                QSKIP("files here can be deleted anyway");
            }
            const QVariantMap result = history.clear(QStringLiteral("all"), true);
            QVERIFY(!result.value(QStringLiteral("succeeded")).toBool());
            QCOMPARE(result.value(QStringLiteral("left")).toInt(), 0);
        }
        QVERIFY(history.clear(QStringLiteral("all"), true).value(QStringLiteral("succeeded")).toBool());
    }

    // A clear that leaves entries says how many went, how many stayed and
    // which audio files kept them, so the page can say what to free.
    void aFailedClearSaysWhatStayed()
    {
        ApplicationController controller(true);
        QTemporaryDir dir;
        HistoryEntry withAudio = kept(QStringLiteral("held"));
        withAudio.kept = HistoryKeep::Audio;
        withAudio.audioPath = dir.filePath(QStringLiteral("held.m4a"));
        QFile audio(withAudio.audioPath);
        QVERIFY(audio.open(QIODevice::WriteOnly));
        audio.close();
        controller.history()->add(withAudio);
        controller.history()->add(kept(QStringLiteral("free")));
        QuickHistory history(&controller);
        test::LockedFolder locked(dir.path());
        if (!locked.holds()) {
            QSKIP("files here can be deleted anyway");
        }
        QCOMPARE(history.clear(QStringLiteral("all"), false),
                 (QVariantMap{{QStringLiteral("succeeded"), false},
                              {QStringLiteral("cleared"), 1},
                              {QStringLiteral("left"), 1},
                              {QStringLiteral("files"), QStringList{withAudio.audioPath}}}));
        QCOMPARE(textsOf(history.results()), QStringList{QStringLiteral("held")});
    }

    // A Recording copies as its plain text file has it; an edit made to its
    // file is what a later search finds, without opening it first.
    void aRecordingCopiesItsLinesAndSearchFindsItsEditedFile()
    {
        ApplicationController controller(true);
        QTemporaryDir dir;
        QFile transcript(dir.filePath(QStringLiteral("standup.txt")));
        QVERIFY(transcript.open(QIODevice::WriteOnly));
        transcript.write("[00:00:04] me: Ask Jon about the release\n");
        transcript.close();
        HistoryEntry recording;
        recording.kind = HistoryKind::Recording;
        recording.startedAt = QDateTime::currentDateTime().addSecs(-60);
        recording.title = QStringLiteral("Standup");
        recording.transcriptPath = transcript.fileName();
        recording.kept = HistoryKeep::Text;
        HistoryStore *store = controller.history();
        const qint64 id = store->add(recording);
        store->appendLine(id, {4000, QStringLiteral("me"), QStringLiteral("Ask Jon about the release")});
        store->finishRecording(id, QDateTime::currentDateTime(), 60000, {}, {});
        QuickHistory history(&controller);

        history.copy(id, false);
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("[00:00:04] me: Ask Jon about the release\n"));

        QVERIFY(transcript.open(QIODevice::WriteOnly));
        transcript.write("[00:00:04] me: Ask John about the release\n");
        transcript.close();
        QVERIFY(transcript.open(QIODevice::ReadWrite));
        QVERIFY(transcript.setFileTime(QDateTime::currentDateTime().addSecs(5), QFileDevice::FileModificationTime));
        transcript.close();
        history.setQuery({{QStringLiteral("text"), QStringLiteral("John")}});
        QTRY_VERIFY(!history.searching());
        QCOMPARE(history.results().size(), 1);
        QCOMPARE(history.results().first().toMap().value(QStringLiteral("id")).toLongLong(), id);
    }
};

int runQuickHistoryTests(int argc, char **argv)
{
    QuickHistoryTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_quick_history.moc"
