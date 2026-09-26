#include "common/test_prelude.h"
#include "core/InsightsLog.h"
#include "core/InsightsSummary.h"

#include <QTemporaryDir>

using namespace speecher;

namespace {

// Saturday 26 September 2026, the mockup's today.
const QDate kToday(2026, 9, 26);

DictationRecord recordOn(const QDate &date, int words = 10, int audioMs = 5000)
{
    return {QDateTime(date, QTime(10, 0)), audioMs, words, QStringLiteral("Kate"), WritingProfile::Other};
}

QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void writeAll(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

} // namespace

class InsightsTests : public QObject {
    Q_OBJECT

private slots:
    void countWordsFollowsWordBoundaries()
    {
        QCOMPARE(countWords(QStringLiteral("Hello, world!")), 2);
        QVERIFY(countWords(QStringLiteral("東京駅に行く")) > 0);
        QCOMPARE(countWords(QString()), 0);
    }

    void logAppendsOneLinePerRecordAndReloads()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("insights.jsonl"));
        const DictationRecord first{QDateTime(QDate(2025, 10, 3), QTime(9, 55)), 38000, 90,
                                    QStringLiteral("Thunderbird"), WritingProfile::Email};
        const DictationRecord second{QDateTime(QDate(2025, 10, 3), QTime(10, 12)), 50000, 108,
                                     QStringLiteral("Claude Code"), WritingProfile::AiCoding};
        {
            InsightsLog log(path);
            QSignalSpy changed(&log, &InsightsLog::changed);
            log.append(first);
            log.append(second);
            QCOMPARE(changed.count(), 2);
        }
        QCOMPARE(readAll(path),
                 QByteArrayLiteral(
                     R"({"finishedAt":"2025-10-03T09:55:00","audioMs":38000,"words":90,"app":"Thunderbird","profile":"email"})"
                     "\n"
                     R"({"finishedAt":"2025-10-03T10:12:00","audioMs":50000,"words":108,"app":"Claude Code","profile":"ai_coding"})"
                     "\n"));

        const InsightsLog reloaded(path);
        QCOMPARE(reloaded.records().size(), 2);
        const DictationRecord &loaded = reloaded.records().at(1);
        QCOMPARE(loaded.finishedAt, second.finishedAt);
        QCOMPARE(loaded.audioMs, 50000);
        QCOMPARE(loaded.words, 108);
        QCOMPARE(loaded.appName, QStringLiteral("Claude Code"));
        QCOMPARE(loaded.profile, WritingProfile::AiCoding);
    }

    void logSkipsACorruptLine()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("insights.jsonl"));
        writeAll(path,
                 R"({"finishedAt":"2025-10-03T09:55:00","audioMs":38000,"words":90,"app":"Slack","profile":"work"})"
                 "\n{\"finishedAt\":\"2025-10-03T1\n"
                 R"({"finishedAt":"2025-10-04T09:55:00","audioMs":1000,"words":3,"app":"Slack","profile":"work"})"
                 "\n");
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("insights log skipped")));
        const InsightsLog log(path);
        QCOMPARE(log.records().size(), 2);
        QCOMPARE(log.records().at(1).words, 3);
    }

    void clearDeletesTheFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("insights.jsonl"));
        InsightsLog log(path);
        log.append(recordOn(kToday));
        QVERIFY(QFile::exists(path));
        QVERIFY(log.clear());
        QVERIFY(!QFile::exists(path));
        QVERIFY(log.records().isEmpty());
    }

    void clearKeepsEverythingWhenTheFileCannotBeDeleted()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("insights.jsonl"));
        InsightsLog log(path);
        log.append(recordOn(kToday));
        const QFileDevice::Permissions writable = QFile::permissions(dir.path());
        QFile::setPermissions(dir.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        QSignalSpy changed(&log, &InsightsLog::changed);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("could not be deleted")));
        const bool cleared = log.clear();
        QFile::setPermissions(dir.path(), writable);
        QVERIFY(!cleared);
        QVERIFY(QFile::exists(path));
        QCOMPARE(log.records().size(), 1);
        QCOMPARE(changed.count(), 0);
    }

    void readOnlyLogNeverTouchesItsFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("seed.jsonl"));
        const QByteArray seed =
            R"({"finishedAt":"2025-10-03T09:55:00","audioMs":38000,"words":90,"app":"Slack","profile":"work"})"
            "\n";
        writeAll(path, seed);
        InsightsLog log(path, InsightsLog::Access::ReadOnly);
        log.append(recordOn(kToday));
        QCOMPARE(log.records().size(), 2);
        QCOMPARE(readAll(path), seed);
        QVERIFY(log.clear());
        QVERIFY(log.records().isEmpty());
        QCOMPARE(readAll(path), seed);
    }

    void mockupSeedLoads()
    {
        const QString seed = QFINDTESTDATA("../docs/insights-mockup/seed-active.jsonl");
        QVERIFY(!seed.isEmpty());
        QCOMPARE(InsightsLog(seed, InsightsLog::Access::ReadOnly).records().size(), 2015);
    }

    void bookComparisonMatchesTheMockup_data()
    {
        QTest::addColumn<int>("words");
        QTest::addColumn<QString>("text");
        QTest::newRow("150") << 150 << QStringLiteral("About half the Gettysburg Address");
        QTest::newRow("9000") << 9000 << QStringLiteral("About half of Macbeth");
        QTest::newRow("15000") << 15000 << QStringLiteral("About half of Hamlet");
        QTest::newRow("80000") << 80000
                               << QStringLiteral("About as long as the first Harry Potter book");
        QTest::newRow("180000") << 180000
                                << QStringLiteral("About as long as The Fellowship of the Ring");
        QTest::newRow("3500000") << 3500000
                                 << QStringLiteral("About 6 times the length of War and Peace");
    }

    void bookComparisonMatchesTheMockup()
    {
        QFETCH(int, words);
        QFETCH(QString, text);
        const InsightsSummary summary =
            summarize({recordOn(kToday, words)}, InsightsRange::AllTime, kToday);
        QCOMPARE(summary.bookComparison, text);
    }

    void streakSurvivesAnIdleToday()
    {
        const InsightsSummary summary = summarize(
            {recordOn(kToday.addDays(-3)), recordOn(kToday.addDays(-2)), recordOn(kToday.addDays(-1))},
            InsightsRange::AllTime, kToday);
        QCOMPARE(summary.currentStreak, 3);
        QCOMPARE(summary.bestStreak, 3);
        QVERIFY(!summary.bestStreakEndsToday);
    }

    void aMissedDayBreaksTheStreak()
    {
        // Active three days ago and yesterday, idle the day before yesterday.
        const InsightsSummary summary = summarize(
            {recordOn(kToday.addDays(-3)), recordOn(kToday.addDays(-1))}, InsightsRange::AllTime, kToday);
        QCOMPARE(summary.currentStreak, 1);
    }

    void bestStreakSpansHistory()
    {
        QList<DictationRecord> records;
        for (int day = 1; day <= 5; ++day) {
            records.append(recordOn(QDate(2026, 8, day)));
        }
        records.append(recordOn(kToday.addDays(-1)));
        records.append(recordOn(kToday));
        const InsightsSummary summary = summarize(records, InsightsRange::Last7Days, kToday);
        QCOMPARE(summary.currentStreak, 2);
        QCOMPARE(summary.bestStreak, 5);
        QCOMPARE(summary.bestStreakEnd, QDate(2026, 8, 5));
        // Friday and Saturday of this Monday-first week.
        QCOMPARE(summary.weekActivity, (std::array<bool, 7>{false, false, false, false, true, true, false}));
        QCOMPARE(summary.todayIndex, 5);
    }

    void brokenStreakReportsTheRunThatEnded()
    {
        const InsightsSummary summary = summarize(
            {recordOn(QDate(2026, 9, 20)), recordOn(QDate(2026, 9, 21)), recordOn(QDate(2026, 9, 22))},
            InsightsRange::AllTime, kToday);
        QCOMPARE(summary.currentStreak, 0);
        QCOMPARE(summary.brokenStreakLength, 3);
        QCOMPARE(summary.brokenStreakEnded, QDate(2026, 9, 23));
    }

    void deltaComparesWithThePreviousPeriod()
    {
        // This week (Sep 20..26): two dictations, 60 words. The week before
        // (Sep 13..19): one dictation, 40 words.
        const QList<DictationRecord> records{recordOn(QDate(2026, 9, 12), 500),
                                             recordOn(QDate(2026, 9, 15), 40),
                                             recordOn(QDate(2026, 9, 20), 20),
                                             recordOn(kToday, 40)};
        const InsightsSummary week = summarize(records, InsightsRange::Last7Days, kToday);
        QCOMPARE(week.words, 60);
        QCOMPARE(week.wordsDelta, std::optional<int>(50));
        QCOMPARE(week.dictationsDelta, std::optional<int>(100));
        QCOMPARE(week.deltaPeriodLabel, QStringLiteral("week"));

        const InsightsSummary year = summarize(records, InsightsRange::ThisYear, kToday);
        QVERIFY(!year.wordsDelta);
        QVERIFY(!year.dictationsDelta);
    }

    void noDeltaWhenThePreviousPeriodIsEmpty()
    {
        const InsightsSummary summary =
            summarize({recordOn(kToday)}, InsightsRange::Last30Days, kToday);
        QVERIFY(!summary.wordsDelta);
        QVERIFY(!summary.dictationsDelta);
    }
};

int runInsightsTests(int argc, char **argv)
{
    InsightsTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_insights.moc"
