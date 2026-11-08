// The import of the insights log into History, which must leave Home's
// numbers as they were, and the seed files screenshots read.

#include "common/test_suites.h"
#include "core/InsightsExport.h"
#include "core/InsightsSummary.h"
#include "core/history/HistoryImport.h"
#include "core/history/HistoryStore.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace speecher;

namespace {

// Saturday 26 September 2026, the last day of data/insights/seed-active.jsonl.
const QDate kToday(2026, 9, 26);

void writeAll(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

// The log as the insights log read it: each line's fields, its time local.
QList<DictationRecord> readTheOldWay(const QString &path)
{
    QList<DictationRecord> records;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return records;
    }
    while (!file.atEnd()) {
        const QJsonObject line = QJsonDocument::fromJson(file.readLine()).object();
        DictationRecord record;
        record.finishedAt = QDateTime::fromString(line[u"finishedAt"].toString(), Qt::ISODate);
        record.audioMs = line[u"audioMs"].toInt();
        record.words = line[u"words"].toInt();
        record.appName = line[u"app"].toString();
        record.profile = writingProfileFromName(line[u"profile"].toString());
        record.profileName = line[u"profileName"].toString();
        records.append(record);
    }
    return records;
}

} // namespace

class HistoryImportTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    }

    // Every period's numbers, as Home's export writes them, are the same
    // from History as they were from the log.
    void homesNumbersSurviveTheImport()
    {
        QTemporaryDir dir;
        const QString log = dir.filePath(QStringLiteral("insights.jsonl"));
        QVERIFY(QFile::copy(QFINDTESTDATA("data/insights/seed-active.jsonl"), log));
        const QList<DictationRecord> before = readTheOldWay(log);
        QCOMPARE(before.size(), 2015);

        HistoryStore store(dir.filePath(QStringLiteral("history.sqlite3")), QString());
        QVERIFY(importInsightsLog(store, log));
        QCOMPARE(store.records().size(), before.size());
        for (const InsightsRange range :
             {InsightsRange::Last7Days, InsightsRange::Last30Days, InsightsRange::ThisYear, InsightsRange::AllTime}) {
            QCOMPARE(insightsJson(summarize(store.records(), range, kToday), range, kToday),
                     insightsJson(summarize(before, range, kToday), range, kToday));
        }
        // Stats only: nothing to list or search.
        QVERIFY(store.search({}).isEmpty());
    }

    // Read once: the log is renamed, so the next start imports nothing.
    void theLogIsImportedOnce()
    {
        QTemporaryDir dir;
        const QString log = dir.filePath(QStringLiteral("insights.jsonl"));
        writeAll(log, R"({"finishedAt":"2025-10-03T09:55:00","audioMs":38000,"words":90,"app":"Slack","profile":"work"})"
                      "\n");
        HistoryStore store(QStringLiteral(":memory:"), QString());
        QVERIFY(importInsightsLog(store, log));
        QVERIFY(!QFile::exists(log));
        QVERIFY(QFile::exists(log + QStringLiteral(".imported")));
        QVERIFY(importInsightsLog(store, log));
        QCOMPARE(store.records().size(), 1);
    }

    // A custom profile's name and the services that ran come across; a line
    // written before providers were kept reads as none, and a corrupt one is
    // skipped.
    void eachLineKeepsWhatItRecorded()
    {
        QTemporaryDir dir;
        const QString log = dir.filePath(QStringLiteral("insights.jsonl"));
        writeAll(log,
                 R"({"finishedAt":"2026-09-23T10:00:00","audioMs":1000,"words":3,"app":"Slack","profile":"work"})"
                 "\n{\"finishedAt\":\"2025-10-03T1\n"
                 R"({"finishedAt":"2026-09-24T09:00:00","audioMs":1000,"words":5,"app":"Zulip","profile":"custom_standup",)"
                 R"("profileName":"Standup","speechProviders":["codex","local"],"refinementProviders":["openai"]})"
                 "\n");
        HistoryStore store(QStringLiteral(":memory:"), QString());
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("insights log skipped unreadable line 2")));
        QVERIFY(importInsightsLog(store, log));
        const QList<DictationRecord> &records = store.records();
        QCOMPARE(records.size(), 2);
        QVERIFY(records.at(0).speechProviders.isEmpty());
        QCOMPARE(records.at(1).finishedAt, QDateTime(QDate(2026, 9, 24), QTime(9, 0)));
        QCOMPARE(records.at(1).profile, QStringLiteral("custom_standup"));
        QCOMPARE(records.at(1).profileName, QStringLiteral("Standup"));
        QCOMPARE(records.at(1).speechProviders, (QStringList{QStringLiteral("codex"), QStringLiteral("local")}));
        QCOMPARE(records.at(1).refinementProviders, QStringList{QStringLiteral("openai")});
    }

    // A log that can't be read stays for the next start, and nothing is added.
    void aFailedImportLeavesTheLog()
    {
        QTemporaryDir dir;
        const QString log = dir.filePath(QStringLiteral("insights.jsonl"));
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("insights.jsonl/not-a-file")));
        HistoryStore store(QStringLiteral(":memory:"), QString());
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("not imported")));
        QVERIFY(!importInsightsLog(store, log));
        QVERIFY(QFileInfo(log).isDir());
        QVERIFY(store.records().isEmpty());
    }

    // A history seed's entries keep their text, kind and title.
    void aHistorySeedKeepsItsWords()
    {
        QTemporaryDir dir;
        const QString seed = dir.filePath(QStringLiteral("history-seed.json"));
        writeAll(seed, R"([
            {"finishedAt":"2026-11-08T14:02:00","audioMs":6000,"words":11,"app":"Slack","profile":"work",
             "text":"Can you send me the Q3 deck?","rawText":"can you send me the q3 deck"},
            {"kind":"file","finishedAt":"2026-11-07T17:20:00","audioMs":1680000,"words":4102,"profile":"other",
             "title":"interview-03.m4a","text":"We spoke about the budget."},
            {"finishedAt":"2026-11-06T09:00:00","words":3,"app":"Kate"}
        ])");
        const std::optional<QList<HistoryEntry>> entries = readHistorySeed(seed);
        QVERIFY(entries);
        QCOMPARE(entries->size(), 3);
        QCOMPARE(entries->at(0).text, std::optional<QString>(QStringLiteral("Can you send me the Q3 deck?")));
        QCOMPARE(entries->at(0).rawText, std::optional<QString>(QStringLiteral("can you send me the q3 deck")));
        QCOMPARE(entries->at(0).kept, HistoryKeep::Text);
        QCOMPARE(entries->at(1).kind, HistoryKind::File);
        QCOMPARE(entries->at(1).outcome, HistoryOutcome::Done);
        QCOMPARE(entries->at(1).title, QStringLiteral("interview-03.m4a"));
        QCOMPARE(entries->at(2).kept, HistoryKeep::Stats);
        QVERIFY(!readHistorySeed(dir.filePath(QStringLiteral("missing.json"))));
    }
};

int runHistoryImportTests(int argc, char **argv)
{
    HistoryImportTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_history_import.moc"
