#pragma once

#include "core/DictationRecord.h"

#include <QDate>
#include <QList>
#include <QMap>
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
    qint64 words = 0;
    qint64 audioMs = 0;
};

struct AppShare {
    // Empty on the fold of the apps past the top five.
    QString name;
    // The fold: how many apps it holds; 0 for one app.
    int otherApps = 0;
    // The Writing Profile name of the app's latest dictation (see
    // dictationProfileName); empty on the fold.
    QString profileName;
    qint64 words = 0;
    int percent = 0;
};

// One Writing Profile's words in the period, by its id, so two profiles with
// one name stay apart and a renamed one stays one.
struct ProfileShare {
    QString profile;
    // dictationProfileName of the profile's latest dictation in the period.
    QString name;
    qint64 words = 0;
    int percent = 0;
};

// The book a word count is closest to: `times` its length, where times is
// one of a quarter, a third, a half, three-quarters, 1, 2, 3 or 4, or a whole
// number from 5 up past the longest book. No book when the count is under a
// quarter of the shortest.
struct BookComparison {
    QString book;
    double times = 0;
    bool operator==(const BookComparison &) const = default;
};

// A milestone near the all-time words: passed, the next one, or one after it.
struct Milestone {
    enum class State { Passed, Next, Later };
    int words = 0;
    State state = State::Later;
    // How far from the milestone before to this one, 0 to 1.
    double progress = 0;
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
    qint64 words = 0;
};

// Every number Home shows, for one period and one `today`. Streaks, the
// heatmap and the records ignore the period; the rest cover only it.
struct InsightsSummary {
    // The period. Word counts add up in 64 bits: a log can hold more than
    // an int's worth, if only by editing it.
    qint64 words = 0;
    int dictations = 0;
    qint64 audioMs = 0;
    int activeDays = 0;
    int averageAudioMs = 0;
    double dictationsPerActiveDay = 0;
    // Percent change against the previous period of equal length. Only for
    // the 7 and 30 day periods, and only when that period has data.
    std::optional<qint64> wordsDelta;
    std::optional<qint64> dictationsDelta;
    // The period's words against a book.
    BookComparison bookComparison;

    // Streak. A streak survives until the end of today.
    int currentStreak = 0;
    int bestStreak = 0;
    QDate bestStreakEnd;
    bool bestStreakEndsToday = false;
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
    bool hasHourData = false;

    qint64 wordsPerMinute = 0;
    // The typing pace the saving and the speed-up are measured against.
    int typingWordsPerMinute = 0;
    qint64 minutesSavedVersusTyping = 0;

    // Top five apps by words, then a fold of the others when they have words.
    QList<AppShare> apps;
    // Every Writing Profile with words in the period, most first.
    QList<ProfileShare> profiles;

    // Records, all time.
    qint64 allTimeWords = 0;
    // Up to three passed, the next and one after it, smallest first; once
    // every milestone is passed, the last three.
    QList<Milestone> milestones;
    LongestDictation longest;
    BusiestDay busiestDay;
    WordiestDay wordiestDay;
    QDate firstDictation;
};

// The Writing Profile a dictation was in, by its current name; the name the
// record stored when the profile is gone; empty when it stored none.
QString dictationProfileName(const DictationRecord &record, const QList<WritingProfileSettings> &profiles);

// `profiles` names the custom profiles the records' profile names come from
// (see dictationProfileName).
InsightsSummary summarize(const QList<DictationRecord> &records,
                          InsightsRange range,
                          const QDate &today,
                          const QList<WritingProfileSettings> &profiles = {});

// What the activity heatmap colours its days by.
enum class HeatMeasure {
    Dictations,
    Words,
    Audio,
};

// Heatmap colour levels 0..4 for one measure: 0 for no activity, then the
// quartile of the day's value among the days with dictation.
class HeatScale {
public:
    HeatScale(const QList<HeatmapDay> &days, HeatMeasure measure);
    int level(const HeatmapDay &day) const;

private:
    HeatMeasure m_measure;
    std::array<qint64, 3> m_quartiles{};
};

// How strongly each heat level shows the accent over the card, level 0 none.
inline constexpr std::array<double, 5> kHeatStrengths{0, 0.30, 0.52, 0.76, 1};

// Month names over the heatmap's latest `weeksShown` week columns, by column
// (0 is the oldest shown): on the first week starting in each month, never on
// a part month at the left edge or in the last two columns. Fewer weeks shown
// only drops columns from the left, so the labels for any width are the tail
// of those for every week.
QMap<int, QString> monthLabels(const QList<HeatmapDay> &heatmap, int weeksShown);

} // namespace speecher
