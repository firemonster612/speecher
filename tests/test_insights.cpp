#include "common/test_prelude.h"
#include "core/InsightsExport.h"
#include "core/InsightsLog.h"
#include "core/InsightsSummary.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

using namespace speecher;

namespace {

// Saturday 26 September 2026, the last day of data/insights/seed-active.jsonl.
const QDate kToday(2026, 9, 26);

DictationRecord recordOn(const QDate &date, int words = 10, int audioMs = 5000)
{
    return {QDateTime(date, QTime(10, 0)), audioMs, words, QStringLiteral("Kate"), WritingProfile::Other};
}

DictationRecord recordAt(const QDate &date, int hour)
{
    return {QDateTime(date, QTime(hour, 0)), 5000, 10, QStringLiteral("Kate"), WritingProfile::Other};
}

DictationRecord recordIn(const QString &app, int words, const QString &profile = WritingProfile::Other)
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

    // A fallback that stood in shows in the record; a line written before
    // providers were kept reads as none.
    void aRecordKeepsTheProvidersThatRanInOrder()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("insights.jsonl"));
        writeAll(path, R"({"finishedAt":"2026-09-23T10:00:00","audioMs":1000,"words":3,"app":"Slack","profile":"work"})"
                       "\n");
        {
            InsightsLog log(path);
            DictationRecord record{QDateTime(kToday, QTime(9, 0)), 1000, 5, QStringLiteral("Kate"), WritingProfile::Other};
            record.speechProviders = {QStringLiteral("codex"), QStringLiteral("local")};
            record.refinementProviders = {QStringLiteral("openai")};
            log.append(record);
        }
        QVERIFY(readAll(path).endsWith(
            R"("profile":"other","speechProviders":["codex","local"],"refinementProviders":["openai"]})"
            "\n"));
        const InsightsLog log(path);
        QCOMPARE(log.records().size(), 2);
        QCOMPARE(log.records().at(0).speechProviders, QStringList());
        QCOMPARE(log.records().at(0).refinementProviders, QStringList());
        QCOMPARE(log.records().at(1).speechProviders, (QStringList{QStringLiteral("codex"), QStringLiteral("local")}));
        QCOMPARE(log.records().at(1).refinementProviders, QStringList{QStringLiteral("openai")});
    }

    // A custom profile's record stores its name; a record from before that
    // has none. Once the profile is gone the first reads by that name and the
    // second has none.
    void aRecordOfADeletedProfileKeepsItsLabel()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("insights.jsonl"));
        {
            InsightsLog log(path);
            log.append({QDateTime(kToday, QTime(9, 0)), 1000, 5, QStringLiteral("Zulip"),
                        QStringLiteral("custom_standup"), QStringLiteral("Standup")});
        }
        QFile file(path);
        QVERIFY(file.open(QIODevice::Append | QIODevice::Text));
        file.write(R"({"finishedAt":"2026-09-23T10:00:00","audioMs":1000,"words":3,"app":"Slack","profile":"custom_old"})"
                   "\n");
        file.close();
        const InsightsLog log(path);
        const QList<WritingProfileSettings> kept{{QStringLiteral("custom_standup"), QStringLiteral("balanced"),
                                                  QStringLiteral("none"), QString(), QStringLiteral("Daily")}};
        QCOMPARE(summarize(log.records(), InsightsRange::AllTime, kToday, kept).apps.at(0).profileName,
                 QStringLiteral("Daily"));
        const InsightsSummary deleted = summarize(log.records(), InsightsRange::AllTime, kToday);
        QCOMPARE(deleted.apps.at(0).profileName, QStringLiteral("Standup"));
        QVERIFY(deleted.apps.at(1).profileName.isEmpty());
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
        // A non-empty directory where the log file should be: QFile::remove
        // refuses it on every platform, unlike a read-only parent directory,
        // which Windows ignores.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("insights.jsonl"));
        InsightsLog log(path);
        log.append(recordOn(kToday));
        QVERIFY(QFile::remove(path));
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("insights.jsonl/keep")));
        QSignalSpy changed(&log, &InsightsLog::changed);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("could not be deleted")));
        QVERIFY(!log.clear());
        QVERIFY(QFileInfo(path).isDir());
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

    void activeSeedLoads()
    {
        const QString seed = QFINDTESTDATA("data/insights/seed-active.jsonl");
        QVERIFY(!seed.isEmpty());
        QCOMPARE(InsightsLog(seed, InsightsLog::Access::ReadOnly).records().size(), 2015);
    }

    void bookComparisonNamesTheClosestWork_data()
    {
        QTest::addColumn<int>("words");
        QTest::addColumn<QString>("book");
        QTest::addColumn<double>("times");
        QTest::newRow("40") << 40 << QString() << 0.0;
        QTest::newRow("150") << 150 << QStringLiteral("the Gettysburg Address") << 0.5;
        QTest::newRow("1100") << 1100 << QStringLiteral("The Raven") << 1.0;
        QTest::newRow("1200") << 1200 << QStringLiteral("the Declaration of Independence") << 1.0;
        QTest::newRow("3848") << 3848 << QStringLiteral("An Occurrence at Owl Creek Bridge") << 1.0;
        QTest::newRow("9000") << 9000 << QStringLiteral("Macbeth") << 0.5;
        QTest::newRow("15000") << 15000 << QStringLiteral("Hamlet") << 0.5;
        QTest::newRow("80000") << 80000 << QStringLiteral("the first Harry Potter book") << 1.0;
        QTest::newRow("180000") << 180000 << QStringLiteral("The Fellowship of the Ring") << 1.0;
        QTest::newRow("3500000") << 3500000 << QStringLiteral("War and Peace") << 6.0;
    }

    void bookComparisonNamesTheClosestWork()
    {
        QFETCH(int, words);
        QFETCH(QString, book);
        QFETCH(double, times);
        const InsightsSummary summary =
            summarize({recordOn(kToday, words)}, InsightsRange::AllTime, kToday);
        QCOMPARE(summary.bookComparison.book, book);
        QCOMPARE(summary.bookComparison.times, times);
    }

    void bookComparisonSaysWhenItChanges_data()
    {
        QTest::addColumn<int>("words");
        QTest::addColumn<int>("bookWords");
        QTest::addColumn<qint64>("changesAt");
        QTest::addColumn<QString>("nextBook");
        QTest::addColumn<double>("nextTimes");
        QTest::newRow("few sentences") << 40 << 0 << qint64(68) << QStringLiteral("the Gettysburg Address") << 0.25;
        QTest::newRow("3153") << 3153 << 1321 << qint64(3154) << QStringLiteral("An Occurrence at Owl Creek Bridge")
                              << 1.0;
        QTest::newRow("3848") << 3848 << 3763 << qint64(4019) << QStringLiteral("The Tell-Tale Heart") << 2.0;
        QTest::newRow("3500000") << 3500000 << 561304 << qint64(3648476) << QStringLiteral("War and Peace") << 7.0;
        QTest::newRow("negative") << -5 << 0 << qint64(0) << QString() << 0.0;
        QTest::newRow("INT_MAX") << INT_MAX << 561304 << qint64(2147829756) << QStringLiteral("War and Peace")
                                 << 3827.0;
    }

    void bookComparisonSaysWhenItChanges()
    {
        QFETCH(int, words);
        QFETCH(int, bookWords);
        QFETCH(qint64, changesAt);
        QFETCH(QString, nextBook);
        QFETCH(double, nextTimes);
        const InsightsSummary summary =
            summarize({recordOn(kToday, words)}, InsightsRange::AllTime, kToday);
        QCOMPARE(summary.bookComparison.bookWords, bookWords);
        QCOMPARE(summary.bookComparisonChangesAt, changesAt);
        QCOMPARE(summary.nextBookComparison.book, nextBook);
        QCOMPARE(summary.nextBookComparison.times, nextTimes);
    }

    void wordsAddUpPastAnInt()
    {
        const InsightsSummary summary = summarize(
            {recordOn(kToday, 2000000000), recordOn(kToday, 2000000000)}, InsightsRange::AllTime, kToday);
        QCOMPARE(summary.words, 4000000000);
        QCOMPARE(summary.allTimeWords, 4000000000);
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
        QCOMPARE(week.wordsDelta, std::optional<qint64>(50));
        QCOMPARE(week.dictationsDelta, std::optional<qint64>(100));

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

    void hoursFindThePeakAndBusiestWeekday()
    {
        // Twelve at 9 pm on Tuesday, eight at 9 am on Wednesday.
        QList<DictationRecord> records(12, recordAt(QDate(2026, 9, 22), 21));
        records.append(QList<DictationRecord>(8, recordAt(QDate(2026, 9, 23), 9)));
        const InsightsSummary summary = summarize(records, InsightsRange::Last30Days, kToday);
        QCOMPARE(summary.hourCounts[21], 12);
        QCOMPARE(summary.hourCounts[9], 8);
        QCOMPARE(summary.peakHour, 21);
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
        QCOMPARE(summary.apps.at(0).profileName, QStringLiteral("Work"));
        const QList<int> percents{29, 24, 19, 14, 10, 5};
        for (int index = 0; index < percents.size(); ++index) {
            QCOMPARE(summary.apps.at(index).percent, percents.at(index));
        }
        QVERIFY(summary.apps.last().name.isEmpty());
        QCOMPARE(summary.apps.last().otherApps, 2);
        QCOMPARE(summary.apps.last().words, 10);
        QVERIFY(summary.apps.last().profileName.isEmpty());
    }

    void profilesShareThePeriodsWordsMostFirst()
    {
        const InsightsSummary summary = summarize(
            {recordIn(QStringLiteral("A"), 30, WritingProfile::Email),
             recordIn(QStringLiteral("B"), 50, WritingProfile::Work),
             recordIn(QStringLiteral("C"), 20, WritingProfile::Email),
             recordIn(QStringLiteral("D"), 100),
             {QDateTime(kToday.addDays(-10), QTime(10, 0)), 5000, 400, QStringLiteral("A"), WritingProfile::Work}},
            InsightsRange::Last7Days, kToday);
        // 200 words this week; the Work dictation ten days ago is outside it.
        QCOMPARE(summary.profiles.size(), 3);
        QCOMPARE(summary.profiles.at(0).name, QStringLiteral("Other"));
        QCOMPARE(summary.profiles.at(0).words, 100);
        QCOMPARE(summary.profiles.at(0).percent, 50);
        QCOMPARE(summary.profiles.at(1).name, QStringLiteral("Email"));
        QCOMPARE(summary.profiles.at(1).words, 50);
        QCOMPARE(summary.profiles.at(1).percent, 25);
        QCOMPARE(summary.profiles.at(2).name, QStringLiteral("Work"));
        QCOMPARE(summary.profiles.at(2).percent, 25);
    }

    // Profiles add up by id: two custom profiles with one name stay two rows,
    // and a deleted profile renamed before it went stays one, under the name
    // its latest dictation stored.
    void profilesAddUpByIdNotName()
    {
        const auto in = [](const QString &profile, const QString &storedName, int words) {
            return DictationRecord{QDateTime(kToday, QTime(10, 0)), 6000, words, QStringLiteral("Kate"), profile,
                                   storedName};
        };
        const QList<WritingProfileSettings> profiles{
            {QStringLiteral("custom_a"), QStringLiteral("balanced"), QStringLiteral("none"), QString(),
             QStringLiteral("Notes")},
            {QStringLiteral("custom_b"), QStringLiteral("balanced"), QStringLiteral("none"), QString(),
             QStringLiteral("Notes")}};
        const InsightsSummary summary =
            summarize({in(QStringLiteral("custom_a"), QStringLiteral("Notes"), 30),
                       in(QStringLiteral("custom_b"), QStringLiteral("Notes"), 20),
                       in(QStringLiteral("custom_gone"), QStringLiteral("Standup"), 15),
                       in(QStringLiteral("custom_gone"), QStringLiteral("Daily"), 10)},
                      InsightsRange::AllTime, kToday, profiles);
        QCOMPARE(summary.profiles.size(), 3);
        QCOMPARE(summary.profiles.at(0).profile, QStringLiteral("custom_a"));
        QCOMPARE(summary.profiles.at(0).words, 30);
        QCOMPARE(summary.profiles.at(1).profile, QStringLiteral("custom_gone"));
        QCOMPARE(summary.profiles.at(1).name, QStringLiteral("Daily"));
        QCOMPARE(summary.profiles.at(1).words, 25);
        QCOMPARE(summary.profiles.at(2).profile, QStringLiteral("custom_b"));
        QCOMPARE(summary.profiles.at(2).name, QStringLiteral("Notes"));
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
        // 1,000 passed, 10,000 next at 150 of the 9,000 between, then 50,000.
        QCOMPARE(summary.milestones.size(), 3);
        QCOMPARE(summary.milestones.at(0).words, 1000);
        QCOMPARE(summary.milestones.at(0).state, Milestone::State::Passed);
        QCOMPARE(summary.milestones.at(1).words, 10000);
        QCOMPARE(summary.milestones.at(1).state, Milestone::State::Next);
        QCOMPARE(summary.milestones.at(1).progress, 150.0 / 9000);
        QCOMPARE(summary.milestones.at(2).state, Milestone::State::Later);
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
        // The last three, all passed.
        QCOMPARE(summary.milestones.size(), 3);
        QCOMPARE(summary.milestones.at(0).words, 250000);
        QCOMPARE(summary.milestones.at(2).words, 1000000);
        QCOMPARE(summary.milestones.at(2).state, Milestone::State::Passed);
    }

    void paceComparesWithTyping()
    {
        // 288 words in two minutes: 144 wpm; typing them takes 7.2 minutes.
        const InsightsSummary summary =
            summarize({recordOn(kToday, 288, 120000)}, InsightsRange::AllTime, kToday);
        QCOMPARE(summary.wordsPerMinute, 144);
        QCOMPARE(summary.typingWordsPerMinute, 40);
        QCOMPARE(summary.minutesSavedVersusTyping, 5);
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

    void jsonCarriesThePeriodStreakAndActiveDays()
    {
        const QList<DictationRecord> records{recordOn(kToday.addDays(-40), 30), recordOn(kToday, 20)};
        const InsightsSummary summary = summarize(records, InsightsRange::Last30Days, kToday);
        const QJsonObject json =
            QJsonDocument::fromJson(insightsJson(summary, InsightsRange::Last30Days, kToday)).object();
        QCOMPARE(json[u"today"].toString(), QStringLiteral("2026-09-26"));
        QCOMPARE(json[u"period"][u"name"].toString(), QStringLiteral("last 30 days"));
        QCOMPARE(json[u"period"][u"words"].toInt(), 20);
        QCOMPARE(json[u"streak"][u"current"].toInt(), 1);
        QCOMPARE(json[u"records"][u"allTimeWords"].toInt(), 50);
        QCOMPARE(json[u"days"].toArray().size(), 2);
        QCOMPARE(json[u"days"][1][u"date"].toString(), QStringLiteral("2026-09-26"));
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
