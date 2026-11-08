// The UI's History singleton over the controller's History: searching as
// the person types, matches that never become markup, delete with Undo,
// filters and pages.

#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "core/history/HistoryStore.h"
#include "frontend/quick/host/QuickHistory.h"

#include <QSignalSpy>
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
    void clearDeletesASpan()
    {
        ApplicationController controller(true);
        controller.history()->add(kept(QStringLiteral("last week"), HistoryKind::Dictation, 8 * 24 * 60));
        controller.history()->add(kept(QStringLiteral("just now")));
        QuickHistory history(&controller);
        QCOMPARE(history.countIn(QStringLiteral("hour")), 1);
        QCOMPARE(history.countIn(QStringLiteral("all")), 2);
        QVERIFY(history.clear(QStringLiteral("hour"), false));
        QCOMPARE(textsOf(history.results()), QStringList{QStringLiteral("last week")});
        QCOMPARE(controller.history()->records().size(), 2);
        QVERIFY(history.clear(QStringLiteral("all"), true));
        QCOMPARE(history.kept(), 0);
        QVERIFY(controller.history()->records().isEmpty());
    }
};

int runQuickHistoryTests(int argc, char **argv)
{
    QuickHistoryTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_quick_history.moc"
