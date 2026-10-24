#include "core/InsightsSummary.h"

#include <QHash>
#include <QLocale>
#include <QMap>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace speecher {
namespace {

constexpr int kTypingWpm = 40;
constexpr int kHeatmapWeeks = 53;
constexpr int kHourDataMinimum = 20;
constexpr int kTopApps = 5;

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

QString formatNumber(qint64 value)
{
    return QLocale().toString(value);
}

struct Comparison {
    QString text;
    const Book *book = nullptr;
};

// "About half of Hamlet": the book and plain fraction closest to the count.
Comparison compareToBook(qint64 words)
{
    if (words < kBooks[0].words * kShares[0].of) {
        return {QStringLiteral("A few sentences so far")};
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
        say = QStringLiteral("%1 times the length of").arg(formatNumber(roundHalfUp(double(words) / last.words)));
    }
    return {QStringLiteral("About %1 %2").arg(say, QLatin1String(bestBook->title)), bestBook};
}

// The smallest count above `words` that reads differently, or 0 if none does.
// Each text covers a single run of counts, so a binary search finds its end.
qint64 nextChange(qint64 words)
{
    constexpr qint64 most = std::numeric_limits<qint64>::max();
    if (words < 0 || words == most) return 0;
    const QString now = compareToBook(words).text;
    qint64 same = words;
    qint64 other = words + 1;
    while (compareToBook(other).text == now) {
        if (other == most) return 0;
        same = other;
        other = other > most / 2 ? most : other * 2;
    }
    while (other - same > 1) {
        const qint64 middle = same + (other - same) / 2;
        (compareToBook(middle).text == now ? same : other) = middle;
    }
    return other;
}

QString capitalized(QString text)
{
    text[0] = text[0].toUpper();
    return text;
}

void describeAsBook(qint64 words, InsightsSummary &summary)
{
    if (words == 0) {
        summary.bookComparison = QStringLiteral("Nothing yet");
        return;
    }
    const Comparison comparison = compareToBook(words);
    summary.bookComparison = comparison.text;
    QStringList tip;
    if (comparison.book) {
        tip << QStringLiteral("%1 is about %2 words")
                   .arg(capitalized(QLatin1String(comparison.book->title)),
                        formatNumber(comparison.book->words));
    }
    if (const qint64 next = nextChange(words)) {
        QString nextText = compareToBook(next).text;
        nextText[0] = nextText[0].toLower();
        tip << QStringLiteral("Changes to %1 at %2 words").arg(nextText, formatNumber(next));
    }
    summary.bookComparisonTip = tip.join(u'\n');
}

std::optional<qint64> percentChange(qint64 current, qint64 previous)
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
        app->profileLabel = dictationProfileLabel(record, profiles);
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
        summary.apps.append({others == 1 ? QStringLiteral("1 other app")
                                         : QStringLiteral("%1 other apps").arg(others),
                             QString(), rest});
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
        const QString label = dictationProfileLabel(record, profiles);
        auto share = std::find_if(summary.profiles.begin(), summary.profiles.end(),
                                  [&](const ProfileShare &each) { return each.label == label; });
        if (share == summary.profiles.end()) {
            summary.profiles.append({label});
            share = summary.profiles.end() - 1;
        }
        share->words += record.words;
    }
    std::stable_sort(summary.profiles.begin(), summary.profiles.end(),
                     [](const ProfileShare &a, const ProfileShare &b) { return a.words > b.words; });
    for (ProfileShare &share : summary.profiles) {
        share.percent = percentOf(share.words, summary.words);
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
    for (int milestone : kWordMilestones) {
        if (milestone <= summary.allTimeWords) {
            summary.passedMilestone = milestone;
        } else if (summary.nextMilestone == 0) {
            summary.nextMilestone = milestone;
        }
    }
}

} // namespace

QString dictationProfileLabel(const DictationRecord &record, const QList<WritingProfileSettings> &profiles)
{
    const QString label = writingProfileLabel(record.profile, profiles);
    if (!label.isEmpty()) return label;
    return record.profileName.isEmpty() ? QStringLiteral("Deleted profile") : record.profileName;
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
        qint64 previousWords = 0;
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

QString audioTotalText(qint64 audioMs)
{
    constexpr qint64 hourMs = 3600 * 1000;
    if (audioMs >= hourMs) {
        return QStringLiteral("%1 hours").arg(QLocale().toString(double(audioMs) / hourMs, 'f', 1));
    }
    return QStringLiteral("%1 min").arg(roundHalfUp(audioMs / 60000.0));
}

QString hourLabel(int hour)
{
    // A no-break space keeps "10 am" on one line in a wrapped sentence.
    return QStringLiteral("%1\u00a0%2").arg(hour % 12 == 0 ? 12 : hour % 12).arg(hour < 12 ? u"am" : u"pm");
}

} // namespace speecher
