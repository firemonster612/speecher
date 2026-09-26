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

DictationRecord recordAt(const QDate &date, int hour)
{
    return {QDateTime(date, QTime(hour, 0)), 5000, 10, QStringLiteral("Kate"), WritingProfile::Other};
}

DictationRecord recordIn(const QString &app, int words, WritingProfile profile = WritingProfile::Other)
{
    return {QDateTime(kToday, QTime(10, 0)), 5000, words, app, profile};
}

HeatmapDay dayWith(int dictations, int words = 0)
{
    return {kToday, dictations, words, 0};
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
    void initTestCase()
    {
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    }

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

    void appendAfterATruncatedLineKeepsTheNewRecord()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("insights.jsonl"));
        writeAll(path,
                 R"({"finishedAt":"2025-10-03T09:55:00","audioMs":38000,"words":90,"app":"Slack","profile":"work"})"
                 "\n{\"finishedAt\":\"2025-10-03T1");
        {
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("insights log skipped")));
            InsightsLog log(path);
            log.append(recordOn(kToday, 7));
        }
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("insights log skipped")));
        const InsightsLog reloaded(path);
        QCOMPARE(reloaded.records().size(), 2);
        QCOMPARE(reloaded.records().at(1).words, 7);
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

    void audioTotalsPastIntMaxDoNotOverflow()
    {
        // 25 dictations of 100,000,000 ms: 2.5 billion ms, above INT_MAX.
        const QList<DictationRecord> records(25, recordOn(kToday, 10, 100'000'000));
        const InsightsSummary summary = summarize(records, InsightsRange::AllTime, kToday);
        QCOMPARE(summary.audioMs, 2'500'000'000LL);
        QCOMPARE(summary.averageAudioMs, 100'000'000);
        QCOMPARE(summary.heatmap.last().audioMs, 2'500'000'000LL);
    }

    void hoursFindThePeakPersonaAndBusiestWeekday()
    {
        // Twelve at 9 pm on Tuesday, eight at 9 am on Wednesday.
        QList<DictationRecord> records(12, recordAt(QDate(2026, 9, 22), 21));
        records.append(QList<DictationRecord>(8, recordAt(QDate(2026, 9, 23), 9)));
        const InsightsSummary summary = summarize(records, InsightsRange::Last30Days, kToday);
        QCOMPARE(summary.hourCounts[21], 12);
        QCOMPARE(summary.hourCounts[9], 8);
        QCOMPARE(summary.peakHour, 21);
        QCOMPARE(summary.persona, QStringLiteral("Night owl"));
        QCOMPARE(summary.busiestWeekday, Qt::Tuesday);
        QVERIFY(summary.hasHourData);

        records.removeLast();
        QVERIFY(!summarize(records, InsightsRange::Last30Days, kToday).hasHourData);
    }

    void appsKeepTheTopFiveAndFoldTheRest()
    {
        const InsightsSummary summary = summarize(
            {recordIn(QStringLiteral("A"), 30, WritingProfile::Email),
             recordIn(QStringLiteral("A"), 30, WritingProfile::Work),
             recordIn(QStringLiteral("B"), 50), recordIn(QStringLiteral("C"), 40),
             recordIn(QStringLiteral("D"), 30), recordIn(QStringLiteral("E"), 20),
             recordIn(QStringLiteral("F"), 5), recordIn(QStringLiteral("G"), 5)},
            InsightsRange::AllTime, kToday);
        // 210 words: 60, 50, 40, 30, 20 and 10 folded.
        QCOMPARE(summary.apps.size(), 6);
        QCOMPARE(summary.apps.at(0).name, QStringLiteral("A"));
        QCOMPARE(summary.apps.at(0).words, 60);
        QCOMPARE(summary.apps.at(0).profileLabel, QStringLiteral("Work"));
        const QList<int> percents{29, 24, 19, 14, 10, 5};
        for (int index = 0; index < percents.size(); ++index) {
            QCOMPARE(summary.apps.at(index).percent, percents.at(index));
        }
        QCOMPARE(summary.apps.last().name, QStringLiteral("2 other apps"));
        QCOMPARE(summary.apps.last().words, 10);
        QVERIFY(summary.apps.last().profileLabel.isEmpty());
    }

    void recordsFindTheLongestBusiestAndWordiest()
    {
        const QList<DictationRecord> records{
            {QDateTime(QDate(2026, 9, 1), QTime(10, 0)), 60000, 100, QStringLiteral("Kate"), WritingProfile::Other},
            {QDateTime(QDate(2026, 9, 1), QTime(11, 0)), 30000, 50, QStringLiteral("Kate"), WritingProfile::Other},
            {QDateTime(QDate(2026, 9, 10), QTime(10, 0)), 90000, 200, QStringLiteral("Slack"), WritingProfile::Work},
            {QDateTime(QDate(2026, 9, 20), QTime(10, 0)), 10000, 800, QStringLiteral("Kate"), WritingProfile::Other},
        };
        const InsightsSummary summary = summarize(records, InsightsRange::Last7Days, kToday);
        QCOMPARE(summary.allTimeWords, 1150);
        QCOMPARE(summary.passedMilestone, std::optional<int>(1000));
        QCOMPARE(summary.nextMilestone, 10000);
        QCOMPARE(milestoneText(summary), QStringLiteral("8,850 to go. You passed 1,000 already."));
        QCOMPARE(summary.longest.audioMs, 90000);
        QCOMPARE(summary.longest.words, 200);
        QCOMPARE(summary.longest.appName, QStringLiteral("Slack"));
        QCOMPARE(summary.longest.date, QDate(2026, 9, 10));
        QCOMPARE(summary.busiestDay.date, QDate(2026, 9, 1));
        QCOMPARE(summary.busiestDay.dictations, 2);
        QCOMPARE(summary.wordiestDay.date, QDate(2026, 9, 20));
        QCOMPARE(summary.wordiestDay.words, 800);
        QCOMPARE(summary.firstDictation, QDate(2026, 9, 1));
    }

    void everyMilestonePassed()
    {
        const InsightsSummary summary =
            summarize({recordOn(kToday, 1'200'000)}, InsightsRange::AllTime, kToday);
        QCOMPARE(summary.nextMilestone, 0);
        QCOMPARE(milestoneText(summary), QStringLiteral("You passed 1,000,000 words"));
    }

    void relativeDayNamesRecentDays()
    {
        QCOMPARE(relativeDay(kToday, kToday), QStringLiteral("today"));
        QCOMPARE(relativeDay(kToday.addDays(-1), kToday), QStringLiteral("yesterday"));
        QCOMPARE(relativeDay(QDate(2026, 9, 22), kToday), QStringLiteral("Tuesday"));
        QCOMPARE(relativeDay(QDate(2026, 9, 19), kToday), QStringLiteral("Sep 19, 2026"));
    }

    void paceComparesWithTyping()
    {
        // 288 words in two minutes: 144 wpm; typing them takes 7.2 minutes.
        const InsightsSummary summary =
            summarize({recordOn(kToday, 288, 120000)}, InsightsRange::AllTime, kToday);
        QCOMPARE(summary.wordsPerMinute, 144);
        QCOMPARE(summary.typingWordsPerMinute, 40);
        QCOMPARE(summary.minutesSavedVersusTyping, 5);
        QCOMPARE(summary.speedupText,
                 QStringLiteral("That's 3.6× faster than typing at 40 words per minute."));
    }

    void deltaTextWordsTheChange()
    {
        QCOMPARE(deltaText(29, QStringLiteral("30 days")), QStringLiteral("▲ 29% vs previous 30 days"));
        QCOMPARE(deltaText(-4, QStringLiteral("week")), QStringLiteral("▼ 4% vs previous week"));
        QCOMPARE(deltaText(0, QStringLiteral("week")), QStringLiteral("Same as previous week"));
        QCOMPARE(deltaText(std::nullopt, QStringLiteral("week")), QString());
    }

    void streakTextWordsTheRun()
    {
        const auto streakOf = [](const QList<QDate> &days) {
            QList<DictationRecord> records;
            for (const QDate &day : days) records.append(recordOn(day));
            return streakText(summarize(records, InsightsRange::AllTime, kToday), kToday);
        };
        QCOMPARE(streakOf({kToday.addDays(-1), kToday}), QStringLiteral("Your longest yet"));
        QCOMPARE(streakOf({QDate(2026, 8, 1), QDate(2026, 8, 2), QDate(2026, 8, 3), kToday}),
                 QStringLiteral("Best: 3 days"));
        QCOMPARE(streakOf({kToday.addDays(-1)}), QStringLiteral("Dictate today to keep it going"));
        QCOMPARE(streakOf({QDate(2026, 9, 20), QDate(2026, 9, 21), QDate(2026, 9, 22)}),
                 QStringLiteral("3-day run ended Wednesday"));
        QCOMPARE(streakOf({}), QString());
    }

    void audioAndClockText()
    {
        QCOMPARE(audioTotalText(4 * 3'600'000), QStringLiteral("4.0 hours"));
        QCOMPARE(audioTotalText(34 * 60'000), QStringLiteral("34 min"));
        QCOMPARE(audioTotalText(2'500'000'000LL), QStringLiteral("694.4 hours"));
        QCOMPARE(clockText(67'000), QStringLiteral("1:07"));
        InsightsSummary summary;
        QCOMPARE(averageDictationText(summary), QStringLiteral("Nothing yet"));
        summary.dictations = 3;
        summary.averageAudioMs = 67'000;
        QCOMPARE(averageDictationText(summary), QStringLiteral("Average dictation 1:07"));
    }

    void hourLabelUsesTwelveHourTime()
    {
        QCOMPARE(hourLabel(0), QStringLiteral("12\u00a0am"));
        QCOMPARE(hourLabel(10), QStringLiteral("10\u00a0am"));
        QCOMPARE(hourLabel(12), QStringLiteral("12\u00a0pm"));
        QCOMPARE(hourLabel(18), QStringLiteral("6\u00a0pm"));
    }

    void heatScaleLevelsByQuartile()
    {
        // Eight active days of 1..8 dictations: quartiles 2, 4 and 6.
        QList<HeatmapDay> days{dayWith(0)};
        for (int count = 1; count <= 8; ++count) days.append(dayWith(count, 9 - count));
        const HeatScale scale(days, HeatMeasure::Dictations);
        QList<int> levels;
        for (const HeatmapDay &day : days) levels.append(scale.level(day));
        QCOMPARE(levels, (QList<int>{0, 1, 1, 2, 2, 3, 3, 4, 4}));
        // Words run the other way, so the levels do too.
        QCOMPARE(HeatScale(days, HeatMeasure::Words).level(days.at(1)), 4);
    }

    void monthLabelsNameEachMonthsFirstWeek()
    {
        // The heatmap's 53 Mondays run from Sep 22, 2025 to Sep 21, 2026.
        const QList<HeatmapDay> heatmap = summarize({}, InsightsRange::AllTime, kToday).heatmap;
        const QMap<int, QString> year = monthLabels(heatmap, 53);
        QCOMPARE(year.keys(), (QList<int>{2, 6, 10, 15, 19, 23, 28, 32, 36, 41, 45, 50}));
        QCOMPARE(year.value(2), QStringLiteral("Oct"));
        QCOMPARE(year.value(50), QStringLiteral("Sep"));
        // The latest 20 weeks start at May 11: no label on that part month.
        const QMap<int, QString> narrow = monthLabels(heatmap, 20);
        QCOMPARE(narrow, (QMap<int, QString>{{3, QStringLiteral("Jun")}, {8, QStringLiteral("Jul")},
                                             {12, QStringLiteral("Aug")}, {17, QStringLiteral("Sep")}}));
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
