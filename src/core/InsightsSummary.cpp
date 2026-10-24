#include "core/InsightsSummary.h"

#include <QHash>
#include <QLocale>
#include <QMap>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace speecher {
namespace {

constexpr int kTypingWpm = 40;
constexpr int kHeatmapWeeks = 53;
constexpr int kHourDataMinimum = 20;
constexpr int kTopApps = 5;
// The all-time word counts that are milestones, smallest first.
constexpr std::array<int, 7> kWordMilestones{1000, 10000, 50000, 100000, 250000, 500000, 1000000};

// Word counts: the Gettysburg Address from the Bliss copy; The Raven, The
// Tell-Tale Heart and An Occurrence at Owl Creek Bridge from their Project
// Gutenberg texts (ebooks 2151, 2148 and 375), words split at spaces and
// em dashes; the Declaration of Independence from the National Archives
// transcript, without its heading and signatures, counted the same way;
// Shakespeare from Open Source Shakespeare; novels from Nathan Bransford's
// novel word count list.
struct Book {
    const char *title;
    int words;
};
constexpr Book kBooks[] = {
    {"the Gettysburg Address", 272},
    {"The Raven", 1088},
    {"the Declaration of Independence", 1321},
    {"The Tell-Tale Heart", 2146},
    {"An Occurrence at Owl Creek Bridge", 3763},
    {"Macbeth", 17121},
    {"Romeo and Juliet", 24545},
    {"Hamlet", 30557},
    {"The Great Gatsby", 47094},
    {"the first Harry Potter book", 76944},
    {"The Hobbit", 95356},
    {"To Kill a Mockingbird", 100388},
    {"The Fellowship of the Ring", 187790},
    {"Moby-Dick", 209117},
    {"War and Peace", 561304},
};
// Plain fractions and multiples only. Half, whole and twice read easiest, so
// the others need to fit noticeably better to win.
struct Share {
    double of;
    double cost;
};
constexpr Share kShares[] = {
    {1.0 / 4, 0.2}, {1.0 / 3, 0.2}, {1.0 / 2, 0}, {3.0 / 4, 0.2},
    {1, 0},         {2, 0},         {3, 0.2},     {4, 0.2},
};

// JavaScript's Math.round, which the mockup's numbers come from: halves go up.
qint64 roundHalfUp(double value)
{
    return qint64(std::floor(value + 0.5));
}

struct DayTotals {
    int dictations = 0;
    qint64 words = 0;
    qint64 audioMs = 0;
};

QMap<QDate, DayTotals> byDay(const QList<DictationRecord> &records)
{
    QMap<QDate, DayTotals> days;
    for (const DictationRecord &record : records) {
        DayTotals &day = days[record.finishedAt.date()];
        ++day.dictations;
        day.words += record.words;
        day.audioMs += record.audioMs;
    }
    return days;
}

QList<DictationRecord> between(const QList<DictationRecord> &records, const QDate &from, const QDate &to)
{
    QList<DictationRecord> inside;
    for (const DictationRecord &record : records) {
        const QDate date = record.finishedAt.date();
        if ((!from.isValid() || date >= from) && date <= to) {
            inside.append(record);
        }
    }
    return inside;
}

// The book and plain share of it closest to the count.
BookComparison compareToBook(qint64 words)
{
    if (words < kBooks[0].words * kShares[0].of) {
        return {};
    }
    const Book &last = kBooks[std::size(kBooks) - 1];
    if (words > last.words * 5.0) {
        return {QLatin1String(last.title), double(roundHalfUp(double(words) / last.words))};
    }
    const Book *bestBook = nullptr;
    double times = 0;
    double bestScore = 0;
    for (const Book &book : kBooks) {
        for (const Share &share : kShares) {
            const double score = std::abs(std::log(words / (book.words * share.of))) + share.cost;
            if (!bestBook || score < bestScore) {
                bestBook = &book;
                times = share.of;
                bestScore = score;
            }
        }
    }
    return {QLatin1String(bestBook->title), times};
}

std::optional<qint64> percentChange(qint64 current, qint64 previous)
{
    if (previous == 0) {
        return std::nullopt;
    }
    return roundHalfUp(double(current - previous) / previous * 100);
}

void summarizeStreaks(const QMap<QDate, DayTotals> &days, const QDate &today, InsightsSummary &summary)
{
    // A streak survives until the end of today, so an idle today doesn't break it.
    for (QDate day = days.contains(today) ? today : today.addDays(-1); days.contains(day);
         day = day.addDays(-1)) {
        ++summary.currentStreak;
    }
    int run = 0;
    QDate previous;
    for (auto it = days.cbegin(); it != days.cend() && it.key() <= today; ++it) {
        run = previous.isValid() && previous.daysTo(it.key()) == 1 ? run + 1 : 1;
        previous = it.key();
        if (run > summary.bestStreak) {
            summary.bestStreak = run;
            summary.bestStreakEnd = it.key();
        }
    }
    summary.bestStreakEndsToday = summary.bestStreak > 0 && summary.bestStreakEnd == today;
    if (summary.currentStreak == 0 && previous.isValid()) {
        for (QDate day = previous; days.contains(day); day = day.addDays(-1)) {
            ++summary.brokenStreakLength;
        }
        summary.brokenStreakEnded = previous.addDays(1);
    }

    const QDate monday = today.addDays(1 - today.dayOfWeek());
    for (int i = 0; i < 7; ++i) {
        const QDate day = monday.addDays(i);
        summary.weekActivity[i] = day <= today && days.contains(day);
    }
    summary.todayIndex = today.dayOfWeek() - 1;
}

void summarizeHeatmap(const QMap<QDate, DayTotals> &days, const QDate &today, InsightsSummary &summary)
{
    const QDate first = today.addDays(-((kHeatmapWeeks - 1) * 7 + today.dayOfWeek() - 1));
    for (QDate day = first; day <= today; day = day.addDays(1)) {
        const DayTotals totals = days.value(day);
        summary.heatmap.append({day, totals.dictations, totals.words, totals.audioMs});
    }
    const QDate yearAgo = today.addDays(-364);
    summary.activeDaysLastYear = int(std::count_if(days.keyBegin(), days.keyEnd(), [&](const QDate &day) {
        return day >= yearAgo && day <= today;
    }));
}

void summarizeHours(const QList<DictationRecord> &period, InsightsSummary &summary)
{
    QHash<int, int> weekdays;
    for (const DictationRecord &record : period) {
        ++summary.hourCounts[record.finishedAt.time().hour()];
        ++weekdays[record.finishedAt.date().dayOfWeek()];
    }
    summary.peakHour = int(std::max_element(summary.hourCounts.begin(), summary.hourCounts.end())
                           - summary.hourCounts.begin());
    // Sunday first, as the mockup counts, so a tie resolves the same way.
    int busiest = Qt::Sunday;
    for (int day : {Qt::Monday, Qt::Tuesday, Qt::Wednesday, Qt::Thursday, Qt::Friday, Qt::Saturday}) {
        if (weekdays.value(day) > weekdays.value(busiest)) {
            busiest = day;
        }
    }
    summary.busiestWeekday = Qt::DayOfWeek(busiest);
    summary.hasHourData = period.size() >= kHourDataMinimum;
}

int percentOf(qint64 part, qint64 whole)
{
    return whole ? int(roundHalfUp(100.0 * part / whole)) : 0;
}

void summarizeApps(const QList<DictationRecord> &period,
                   const QList<WritingProfileSettings> &profiles,
                   InsightsSummary &summary)
{
    QList<AppShare> totals;
    for (const DictationRecord &record : period) {
        auto app = std::find_if(totals.begin(), totals.end(),
                                [&](const AppShare &share) { return share.name == record.appName; });
        if (app == totals.end()) {
            totals.append({record.appName});
            app = totals.end() - 1;
        }
        app->words += record.words;
        app->profileName = dictationProfileName(record, profiles);
    }
    std::stable_sort(totals.begin(), totals.end(),
                     [](const AppShare &a, const AppShare &b) { return a.words > b.words; });
    summary.apps = totals.mid(0, kTopApps);
    qint64 rest = 0;
    for (qsizetype i = kTopApps; i < totals.size(); ++i) {
        rest += totals[i].words;
    }
    if (rest > 0) {
        const qsizetype others = totals.size() - kTopApps;
        summary.apps.append({QString(), int(others), QString(), rest});
    }
    for (AppShare &app : summary.apps) {
        app.percent = percentOf(app.words, summary.words);
    }
}

void summarizeProfiles(const QList<DictationRecord> &period,
                       const QList<WritingProfileSettings> &profiles,
                       InsightsSummary &summary)
{
    for (const DictationRecord &record : period) {
        auto share = std::find_if(summary.profiles.begin(), summary.profiles.end(),
                                  [&](const ProfileShare &each) { return each.profile == record.profile; });
        if (share == summary.profiles.end()) {
            summary.profiles.append({record.profile});
            share = summary.profiles.end() - 1;
        }
        share->name = dictationProfileName(record, profiles);
        share->words += record.words;
    }
    std::stable_sort(summary.profiles.begin(), summary.profiles.end(),
                     [](const ProfileShare &a, const ProfileShare &b) { return a.words > b.words; });
    for (ProfileShare &share : summary.profiles) {
        share.percent = percentOf(share.words, summary.words);
    }
}

void summarizeMilestones(InsightsSummary &summary)
{
    const auto next = std::find_if(kWordMilestones.begin(), kWordMilestones.end(),
                                   [&](int words) { return words > summary.allTimeWords; });
    const qsizetype nextIndex = next - kWordMilestones.begin();
    const qsizetype first = std::max<qsizetype>(0, nextIndex - 3);
    const qsizetype end = std::min<qsizetype>(qsizetype(kWordMilestones.size()), nextIndex + 2);
    for (qsizetype index = first; index < end; ++index) {
        const int words = kWordMilestones[index];
        const int from = index > 0 ? kWordMilestones[index - 1] : 0;
        const Milestone::State state = index < nextIndex ? Milestone::State::Passed
            : index == nextIndex                         ? Milestone::State::Next
                                                         : Milestone::State::Later;
        summary.milestones.append(
            {words, state, std::clamp(double(summary.allTimeWords - from) / (words - from), 0.0, 1.0)});
    }
}

void summarizeRecords(const QList<DictationRecord> &records,
                      const QMap<QDate, DayTotals> &days,
                      InsightsSummary &summary)
{
    const DictationRecord *longest = nullptr;
    QDateTime first;
    for (const DictationRecord &record : records) {
        summary.allTimeWords += record.words;
        if (!longest || record.audioMs > longest->audioMs) {
            longest = &record;
        }
        if (!first.isValid() || record.finishedAt < first) {
            first = record.finishedAt;
        }
    }
    if (longest) {
        summary.longest = {longest->audioMs, longest->words, longest->appName, longest->finishedAt.date()};
    }
    summary.firstDictation = first.date();
    for (auto it = days.cbegin(); it != days.cend(); ++it) {
        if (it->dictations > summary.busiestDay.dictations) {
            summary.busiestDay = {it.key(), it->dictations};
        }
        if (it->words > summary.wordiestDay.words) {
            summary.wordiestDay = {it.key(), it->words};
        }
    }
    summarizeMilestones(summary);
}

} // namespace

QString dictationProfileName(const DictationRecord &record, const QList<WritingProfileSettings> &profiles)
{
    const QString label = writingProfileLabel(record.profile, profiles);
    return label.isEmpty() ? record.profileName : label;
}

InsightsSummary summarize(const QList<DictationRecord> &records,
                          InsightsRange range,
                          const QDate &today,
                          const QList<WritingProfileSettings> &profiles)
{
    InsightsSummary summary;
    QDate from;
    switch (range) {
    case InsightsRange::Last7Days:
        from = today.addDays(-6);
        break;
    case InsightsRange::Last30Days:
        from = today.addDays(-29);
        break;
    case InsightsRange::ThisYear:
        from = QDate(today.year(), 1, 1);
        break;
    case InsightsRange::AllTime:
        break;
    }

    const QList<DictationRecord> period = between(records, from, today);
    const QMap<QDate, DayTotals> periodDays = byDay(period);
    for (const DictationRecord &record : period) {
        summary.words += record.words;
        summary.audioMs += record.audioMs;
    }
    summary.dictations = int(period.size());
    summary.activeDays = int(periodDays.size());
    if (summary.dictations) {
        summary.averageAudioMs = roundHalfUp(double(summary.audioMs) / summary.dictations);
        summary.dictationsPerActiveDay = double(summary.dictations) / summary.activeDays;
    }
    if (range == InsightsRange::Last7Days || range == InsightsRange::Last30Days) {
        const qint64 span = from.daysTo(today) + 1;
        const QList<DictationRecord> previous = between(records, from.addDays(-span), from.addDays(-1));
        qint64 previousWords = 0;
        for (const DictationRecord &record : previous) {
            previousWords += record.words;
        }
        summary.wordsDelta = percentChange(summary.words, previousWords);
        summary.dictationsDelta = percentChange(summary.dictations, previous.size());
    }
    summary.bookComparison = compareToBook(summary.words);

    summary.typingWordsPerMinute = kTypingWpm;
    if (summary.audioMs > 0) {
        const double minutes = summary.audioMs / 60000.0;
        summary.wordsPerMinute = roundHalfUp(summary.words / minutes);
        const double typingMinutes = double(summary.words) / kTypingWpm;
        summary.minutesSavedVersusTyping = roundHalfUp(std::max(0.0, typingMinutes - minutes));
    }
    summarizeHours(period, summary);
    summarizeApps(period, profiles, summary);
    summarizeProfiles(period, profiles, summary);

    const QMap<QDate, DayTotals> allDays = byDay(records);
    summarizeStreaks(allDays, today, summary);
    summarizeHeatmap(allDays, today, summary);
    summarizeRecords(records, allDays, summary);
    return summary;
}

namespace {

qint64 heatValue(const HeatmapDay &day, HeatMeasure measure)
{
    switch (measure) {
    case HeatMeasure::Words: return day.words;
    case HeatMeasure::Audio: return day.audioMs;
    case HeatMeasure::Dictations: break;
    }
    return day.dictations;
}

} // namespace

HeatScale::HeatScale(const QList<HeatmapDay> &days, HeatMeasure measure)
    : m_measure(measure)
{
    QList<qint64> active;
    for (const HeatmapDay &day : days) {
        if (day.dictations > 0) active.append(heatValue(day, measure));
    }
    if (active.isEmpty()) return;
    std::sort(active.begin(), active.end());
    for (int index = 0; index < 3; ++index) {
        const double p = (index + 1) / 4.0;
        m_quartiles[index] = active[qsizetype(std::floor(p * (active.size() - 1)))];
    }
}

int HeatScale::level(const HeatmapDay &day) const
{
    const qint64 value = heatValue(day, m_measure);
    if (day.dictations == 0 || value == 0) return 0;
    const auto above = std::find_if(m_quartiles.begin(), m_quartiles.end(),
                                    [value](qint64 quartile) { return value <= quartile; });
    return int(above - m_quartiles.begin()) + 1;
}

QMap<int, QString> monthLabels(const QList<HeatmapDay> &heatmap, int weeksShown)
{
    QMap<int, QString> labels;
    const int weeks = int((heatmap.size() + 6) / 7);
    const int first = std::max(0, weeks - weeksShown);
    const QLocale locale;
    for (int column = 0; column < weeks - first - 2; ++column) {
        const QDate monday = heatmap.at((first + column) * 7).date;
        // The week before is in the previous month exactly when this Monday
        // is one of its month's first seven days.
        if (monday.day() <= 7) {
            labels.insert(column, locale.standaloneMonthName(monday.month(), QLocale::ShortFormat));
        }
    }
    return labels;
}

} // namespace speecher
