#pragma once

#include "core/DictationRecord.h"

#include <QDate>
#include <QList>
#include <QString>

#include <array>
#include <optional>

namespace speecher {

enum class InsightsRange {
    Last7Days,
    Last30Days,
    ThisYear,
    AllTime,
};

struct HeatmapDay {
    QDate date;
    int dictations = 0;
    int words = 0;
    qint64 audioMs = 0;
};

struct AppShare {
    QString name;
    // Writing Profile label of the app's latest dictation; empty on the
    // "N other apps" fold.
    QString profileLabel;
    int words = 0;
    int percent = 0;
};

struct LongestDictation {
    int audioMs = 0;
    int words = 0;
    QString appName;
    QDate date;
};

struct BusiestDay {
    QDate date;
    int dictations = 0;
};

struct WordiestDay {
    QDate date;
    int words = 0;
};

// Every number Home shows, for one period and one `today`. Streaks, the
// heatmap and the records ignore the period; the rest cover only it.
struct InsightsSummary {
    // The period.
    int words = 0;
    int dictations = 0;
    qint64 audioMs = 0;
    int activeDays = 0;
    int averageAudioMs = 0;
    double dictationsPerActiveDay = 0;
    // Percent change against the previous period of equal length. Only for
    // the 7 and 30 day periods, and only when that period has data.
    std::optional<int> wordsDelta;
    std::optional<int> dictationsDelta;
    QString deltaPeriodLabel; // "week" or "30 days"; empty for the other periods
    // "About half of Hamlet", plain text; "Nothing yet" with no words.
    QString bookComparison;
    QString bookComparisonTip; // "Hamlet is about 30,557 words"; may be empty

    // Streak. A streak survives until the end of today.
    int currentStreak = 0;
    int bestStreak = 0;
    QDate bestStreakEnd; // for "Ended <relativeDay>"
    bool bestStreakEndsToday = false; // "That's the one you're on"
    // When currentStreak is 0: the last run and the first day it was missed.
    int brokenStreakLength = 0;
    QDate brokenStreakEnded;
    std::array<bool, 7> weekActivity{}; // Monday..Sunday of this week
    int todayIndex = 0;                 // today's slot in weekActivity

    // Every day of the last 53 Monday-first weeks up to today, oldest first.
    QList<HeatmapDay> heatmap;
    int activeDaysLastYear = 0;

    // The period, by local hour.
    std::array<int, 24> hourCounts{};
    int peakHour = 0;
    Qt::DayOfWeek busiestWeekday = Qt::Monday;
    QString persona; // "Morning talker", "Night owl", ...
    bool hasHourData = false;

    int wordsPerMinute = 0;
    int minutesSavedVersusTyping = 0;

    // Top five apps by words, then an "N other apps" fold when the rest has words.
    QList<AppShare> apps;

    // Records, all time.
    int allTimeWords = 0; // the milestone progress
    int nextMilestone = 0; // 0 once every milestone is passed
    std::optional<int> passedMilestone;
    LongestDictation longest;
    BusiestDay busiestDay;
    WordiestDay wordiestDay;
    QDate firstDictation;
};

InsightsSummary summarize(const QList<DictationRecord> &records,
                          InsightsRange range,
                          const QDate &today);

// Heatmap colour level 0..4: 0 for no activity, then the quartile of `value`
// among `activeValues`, the chosen measure on every day with dictation.
int heatLevel(int value, const QList<int> &activeValues);

// "today", "yesterday", a weekday within the last six days, else "Mar 1, 2026".
QString relativeDay(const QDate &date, const QDate &today);

} // namespace speecher
