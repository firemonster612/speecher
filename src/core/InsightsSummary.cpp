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

// Word counts: Shakespeare from Open Source Shakespeare, novels from Nathan
// Bransford's novel word count list, the Gettysburg Address from the Bliss copy.
struct Book {
    const char *title;
    int words;
};
constexpr Book kBooks[] = {
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
constexpr int kGettysburgWords = 272;
// Plain fractions and multiples only. Half, whole and twice read easiest, so
// the others need to fit noticeably better to win.
struct Share {
    double of;
    const char *say;
    double cost;
};
constexpr Share kShares[] = {
    {1.0 / 4, "a quarter of", 0.2},
    {1.0 / 3, "a third of", 0.2},
    {1.0 / 2, "half of", 0},
    {3.0 / 4, "three-quarters of", 0.2},
    {1, "as long as", 0},
    {2, "twice the length of", 0},
    {3, "three times the length of", 0.2},
    {4, "four times the length of", 0.2},
};
constexpr int kMilestones[] = {1000, 10000, 50000, 100000, 250000, 500000, 1000000};

// JavaScript's Math.round, which the mockup's numbers come from: halves go up.
int roundHalfUp(double value)
{
    return int(std::floor(value + 0.5));
}

struct DayTotals {
    int dictations = 0;
    int words = 0;
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

QString formatNumber(qint64 value)
{
    return QLocale().toString(value);
}

QString plural(int count, const QString &noun)
{
    return QStringLiteral("%1 %2").arg(formatNumber(count), count == 1 ? noun : noun + u's');
}

// "About half of Hamlet": the book and plain fraction closest to the count.
void describeAsBook(int words, InsightsSummary &summary)
{
    if (words == 0) {
        summary.bookComparison = QStringLiteral("Nothing yet");
        return;
    }
    const double smallest = kBooks[0].words * kShares[0].of;
    if (words < smallest) {
        const int addresses = roundHalfUp(double(words) / kGettysburgWords);
        const QString tip = QStringLiteral("The Gettysburg Address is %1 words").arg(kGettysburgWords);
        if (words < kGettysburgWords / 4.0) {
            summary.bookComparison = QStringLiteral("A few sentences so far");
        } else if (words < kGettysburgWords * 0.75) {
            summary.bookComparison = QStringLiteral("About half the Gettysburg Address");
            summary.bookComparisonTip = tip;
        } else {
            summary.bookComparison = addresses == 1
                ? QStringLiteral("About the Gettysburg Address")
                : QStringLiteral("About %1 Gettysburg Addresses").arg(addresses);
            summary.bookComparisonTip = tip;
        }
        return;
    }
    const Book *bestBook = nullptr;
    QString say;
    double bestScore = 0;
    for (const Book &book : kBooks) {
        for (const Share &share : kShares) {
            const double score = std::abs(std::log(words / (book.words * share.of))) + share.cost;
            if (!bestBook || score < bestScore) {
                bestBook = &book;
                say = QLatin1String(share.say);
                bestScore = score;
            }
        }
    }
    const Book &last = kBooks[std::size(kBooks) - 1];
    if (words > last.words * 5.0) {
        bestBook = &last;
        say = QStringLiteral("%1 times the length of").arg(roundHalfUp(double(words) / last.words));
    }
    QString title = QLatin1String(bestBook->title);
    summary.bookComparison = QStringLiteral("About %1 %2").arg(say, title);
    if (title.startsWith(QLatin1String("the "))) {
        title[0] = QLatin1Char('T');
    }
    summary.bookComparisonTip =
        QStringLiteral("%1 is about %2 words").arg(title, formatNumber(bestBook->words));
}

std::optional<int> percentChange(qint64 current, qint64 previous)
{
    if (previous == 0) {
        return std::nullopt;
    }
    return roundHalfUp(double(current - previous) / previous * 100);
}

QString personaFor(int hour)
{
    if (hour < 5) return QStringLiteral("Night owl");
    if (hour < 12) return QStringLiteral("Morning talker");
    if (hour < 17) return QStringLiteral("Afternoon talker");
    if (hour < 21) return QStringLiteral("Evening talker");
    return QStringLiteral("Night owl");
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
    summary.persona = personaFor(summary.peakHour);
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

void summarizeApps(const QList<DictationRecord> &period, InsightsSummary &summary)
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
        app->profileLabel = writingProfileLabel(record.profile);
    }
    std::stable_sort(totals.begin(), totals.end(),
                     [](const AppShare &a, const AppShare &b) { return a.words > b.words; });
    summary.apps = totals.mid(0, kTopApps);
    int rest = 0;
    for (qsizetype i = kTopApps; i < totals.size(); ++i) {
        rest += totals[i].words;
    }
    if (rest > 0) {
        const qsizetype others = totals.size() - kTopApps;
        summary.apps.append({others == 1 ? QStringLiteral("1 other app")
                                         : QStringLiteral("%1 other apps").arg(others),
                             QString(), rest});
    }
    for (AppShare &app : summary.apps) {
        app.percent = summary.words ? roundHalfUp(100.0 * app.words / summary.words) : 0;
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
    for (int milestone : kMilestones) {
        if (milestone <= summary.allTimeWords) {
            summary.passedMilestone = milestone;
        } else if (summary.nextMilestone == 0) {
            summary.nextMilestone = milestone;
        }
    }
}

} // namespace

InsightsSummary summarize(const QList<DictationRecord> &records,
                          InsightsRange range,
                          const QDate &today)
{
    InsightsSummary summary;
    QDate from;
    switch (range) {
    case InsightsRange::Last7Days:
        from = today.addDays(-6);
        summary.deltaPeriodLabel = QStringLiteral("week");
        break;
    case InsightsRange::Last30Days:
        from = today.addDays(-29);
        summary.deltaPeriodLabel = QStringLiteral("30 days");
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
    if (!summary.deltaPeriodLabel.isEmpty()) {
        const qint64 span = from.daysTo(today) + 1;
        const QList<DictationRecord> previous = between(records, from.addDays(-span), from.addDays(-1));
        int previousWords = 0;
        for (const DictationRecord &record : previous) {
            previousWords += record.words;
        }
        summary.wordsDelta = percentChange(summary.words, previousWords);
        summary.dictationsDelta = percentChange(summary.dictations, previous.size());
    }
    describeAsBook(summary.words, summary);

    summary.typingWordsPerMinute = kTypingWpm;
    if (summary.audioMs > 0) {
        const double minutes = summary.audioMs / 60000.0;
        summary.wordsPerMinute = roundHalfUp(summary.words / minutes);
        const double typingMinutes = double(summary.words) / kTypingWpm;
        summary.minutesSavedVersusTyping = roundHalfUp(std::max(0.0, typingMinutes - minutes));
        summary.speedupText =
            QStringLiteral("That's %1× faster than typing at %2 words per minute.")
                .arg(QLocale().toString(double(summary.wordsPerMinute) / kTypingWpm, 'f', 1))
                .arg(kTypingWpm);
    }
    summarizeHours(period, summary);
    summarizeApps(period, summary);

    const QMap<QDate, DayTotals> allDays = byDay(records);
    summarizeStreaks(allDays, today, summary);
    summarizeHeatmap(allDays, today, summary);
    summarizeRecords(records, allDays, summary);
    return summary;
}

qint64 heatValue(const HeatmapDay &day, HeatMeasure measure)
{
    switch (measure) {
    case HeatMeasure::Words: return day.words;
    case HeatMeasure::Audio: return day.audioMs;
    case HeatMeasure::Dictations: break;
    }
    return day.dictations;
}

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

QString relativeDay(const QDate &date, const QDate &today)
{
    const qint64 daysAgo = date.daysTo(today);
    if (daysAgo == 0) return QStringLiteral("today");
    if (daysAgo == 1) return QStringLiteral("yesterday");
    const QLocale locale;
    if (daysAgo > 1 && daysAgo < 7) return locale.dayName(date.dayOfWeek(), QLocale::LongFormat);
    return locale.toString(date, QStringLiteral("MMM d, yyyy"));
}

QString deltaText(const std::optional<int> &delta, const QString &period)
{
    if (!delta) return {};
    if (*delta == 0) return QStringLiteral("Same as previous %1").arg(period);
    return QStringLiteral("%1 %2% vs previous %3")
        .arg(*delta > 0 ? QStringLiteral("▲") : QStringLiteral("▼"))
        .arg(std::abs(*delta))
        .arg(period);
}

QString streakText(const InsightsSummary &summary, const QDate &today)
{
    if (summary.currentStreak > 0) {
        if (!summary.weekActivity[summary.todayIndex]) {
            return QStringLiteral("Dictate today to keep it going");
        }
        return summary.currentStreak >= summary.bestStreak
            ? QStringLiteral("Your longest yet")
            : QStringLiteral("Best: %1").arg(plural(summary.bestStreak, QStringLiteral("day")));
    }
    if (summary.brokenStreakLength > 0) {
        return QStringLiteral("%1-day run ended %2")
            .arg(summary.brokenStreakLength)
            .arg(relativeDay(summary.brokenStreakEnded, today));
    }
    return {};
}

QString milestoneText(const InsightsSummary &summary)
{
    if (summary.nextMilestone == 0) {
        return QStringLiteral("You passed %1 words").arg(formatNumber(summary.passedMilestone.value_or(0)));
    }
    QString text = QStringLiteral("%1 to go").arg(formatNumber(summary.nextMilestone - summary.allTimeWords));
    if (summary.passedMilestone) {
        text += QStringLiteral(". You passed %1 already.").arg(formatNumber(*summary.passedMilestone));
    }
    return text;
}

QString audioTotalText(qint64 audioMs)
{
    constexpr qint64 hourMs = 3600 * 1000;
    if (audioMs >= hourMs) {
        return QStringLiteral("%1 hours").arg(QLocale().toString(double(audioMs) / hourMs, 'f', 1));
    }
    return QStringLiteral("%1 min").arg(roundHalfUp(audioMs / 60000.0));
}

QString averageDictationText(const InsightsSummary &summary)
{
    return summary.dictations
        ? QStringLiteral("Average dictation %1").arg(clockText(summary.averageAudioMs))
        : QStringLiteral("Nothing yet");
}

QString clockText(qint64 ms)
{
    const qint64 seconds = (ms + 500) / 1000;
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString hourLabel(int hour)
{
    // A no-break space keeps "10 am" on one line in a wrapped sentence.
    return QStringLiteral("%1\u00a0%2").arg(hour % 12 == 0 ? 12 : hour % 12).arg(hour < 12 ? u"am" : u"pm");
}

} // namespace speecher
