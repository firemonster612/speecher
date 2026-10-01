#pragma once

#include "core/DictationRecord.h"

#include <QDate>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

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
    // "Hamlet is about 30,557 words", then on a new line "Changes to about
    // twice the length of Macbeth at 32,348 words"; empty with no words.
    QString bookComparisonTip;

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
    // The typing pace the saving and the speed-up are measured against.
    int typingWordsPerMinute = 0;
    int minutesSavedVersusTyping = 0;
    // "That's 3.6× faster than typing at 40 words per minute."; empty with no audio.
    QString speedupText;

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

// `profiles` names the custom profiles the records' labels come from. A record
// whose profile is gone reads as the name it stored, or "Deleted profile".
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

// Wording every Home shares.

// "today", "yesterday", a weekday within the last six days, else "Mar 1, 2026".
QString relativeDay(const QDate &date, const QDate &today);
// "▲ 29% vs previous 30 days", "▼ 4% vs previous week", "Same as previous
// week"; empty without a delta.
QString deltaText(const std::optional<int> &delta, const QString &period);
// The line under the streak: "Your longest yet", "Best: 12 days", "Dictate
// today to keep it going", "3-day run ended Tuesday"; empty with no history.
QString streakText(const InsightsSummary &summary, const QDate &today);
// "4,000 to go. You passed 1,000 already.", or "You passed 1,000,000 words"
// once every milestone is.
QString milestoneText(const InsightsSummary &summary);
// "4.0 hours" from an hour up, else "34 min".
QString audioTotalText(qint64 audioMs);
// "Average dictation 1:07", or "Nothing yet" for an empty period.
QString averageDictationText(const InsightsSummary &summary);
// "m:ss".
QString clockText(qint64 ms);
// "12 am", "10 am", "6 pm", with a no-break space.
QString hourLabel(int hour);

// One of the four stat tiles, as every Home and the shared image word it.
struct InsightTileText {
    QString title; // "Words dictated"
    // Platform-neutral, "text", "flame", "microphone" or "waveform"; each
    // front end maps it to an icon of its own.
    QString iconId;
    // The figure large and its unit small: "4" and "days", "4.0" and "hours".
    QString value;
    QString unit;
    // The lines under the figure, empty ones left out, and a tip for the first.
    QStringList lines;
    QString firstLineTip;
    bool showsWeek = false; // the streak's weekday strip goes under its lines
};
// Words, streak, dictations, audio: the order every Home shows them in.
QList<InsightTileText> insightTiles(const InsightsSummary &summary, const QDate &today);

// "1 word", "12 words", and so on, the number localised.
QString wordCountText(int words);
QString dictationCountText(int dictations);
QString dayCountText(int days);
// A weekday's first letter as one grapheme (Monday is 1), whatever the
// locale's narrow names are.
QString weekdayLetter(int dayOfWeek);
// The heatmap's row labels, Monday first: the locale's short names for
// Monday, Wednesday and Friday, and empty for the other rows.
std::array<QString, 7> heatmapRowLabels();
// "12 days with dictation in the last year".
QString activeDaysLastYearText(int days);
// The heatmap's caption for the weeks a narrow one draws: the last year's
// line while all 53 show, else "4 days with dictation in the last 20 weeks",
// counted over those weeks only.
QString heatmapSpanText(const InsightsSummary &summary, int drawnWeeks);
// The words either side of the heatmap's legend.
QString heatLegendLessText();
QString heatLegendMoreText();
// A chart mark's tip: a bold title over the count.
struct ChartTip {
    QString title;
    QString detail;
};
// "10 am to 11 am" over "3 dictations"; "Mail" over "120 words".
ChartTip hourTip(int hour, int dictations);
ChartTip appTip(const AppShare &app);
// "Morning talker." and the sentence after it, on the "When you talk" card.
QString personaText(const InsightsSummary &summary);
QString peakText(const InsightsSummary &summary);
// The learned corrections card.
QString learnedCorrectionsTitle();
// "Correction learned" or "Corrections learned", under the count.
QString learnedCorrectionsCaption(int count);
// The line under it, which says how learning stands: off, held by
// accessibility, waiting for a first correction, or what the list holds.
QString learnedCorrectionsNote(int count, bool learningOn, bool accessibilityOn);
// The button to the corrections page; empty while there is nothing to review
// and nothing to turn on.
QString learnedCorrectionsAction(int count, bool learningOn);

// Home's fixed wording: card titles, captions and the empty notices.
enum class HomeText {
    InsightsOffTitle,
    InsightsOffBody,
    NoInsightsTitle,
    NoInsightsBody,
    InsightsSettings,
    YourDictation,
    Period,
    Activity,
    Measure,
    WhenYouTalk,
    NoHourData,
    Pace,
    SpeakingPace,
    SavedOverTyping,
    YouSpeaking,
    TypicalTyping,
    WhereYourWordsGo,
    NoDictationInPeriod,
    Records,
    PrivacyNote,
    // Clearing the history: the confirmation, its button, and what a failure
    // says to do next.
    ClearHistoryTitle,
    ClearHistoryQuestion,
    ClearHistoryBody,
    ClearHistoryConfirm,
    ClearHistoryFailed,
};
QString homeText(HomeText text);
// "45 min", "2 h", "2 h 5 min": the time Pace says dictation saved.
QString minutesText(int minutes);
// The heatmap's measure picker: "Dictations", "Words", "Minutes of audio".
QString heatMeasureLabel(HeatMeasure measure);
// A heatmap day's tip: what it held under the measure, over its date.
ChartTip heatmapDayTip(const HeatmapDay &day, HeatMeasure measure);

// What each chart says to assistive technology in place of its marks.
QString heatmapDescription(const InsightsSummary &summary, HeatMeasure measure);
QString hourChartDescription(const InsightsSummary &summary);
QString weekDescription(const InsightsSummary &summary);

// One row of the Records card: a title, the line under it and the figure on
// the right. The next-milestone row shows a progress bar in place of a figure.
struct InsightRecordText {
    QString title;
    QString detail;
    QString value;
    bool milestoneBar = false;
};
// The streak record is left out while it is the streak running now, which
// the Streak tile already says.
QList<InsightRecordText> insightRecords(const InsightsSummary &summary, const QDate &today);

} // namespace speecher
