#include "core/InsightsExport.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QStringList>

namespace speecher {
namespace {

QString number(qint64 value)
{
    return QLocale().toString(value);
}

QString plural(qint64 count, const QString &noun)
{
    return QStringLiteral("%1 %2").arg(number(count), count == 1 ? noun : noun + u's');
}

QString speedup(const InsightsSummary &summary)
{
    return QLocale().toString(double(summary.wordsPerMinute) / summary.typingWordsPerMinute, 'f', 1);
}

QString isoDate(const QDate &date)
{
    return date.toString(Qt::ISODate);
}

} // namespace

QString insightsPeriodName(InsightsRange range)
{
    switch (range) {
    case InsightsRange::Last7Days: return QStringLiteral("last 7 days");
    case InsightsRange::Last30Days: return QStringLiteral("last 30 days");
    case InsightsRange::ThisYear: return QStringLiteral("this year");
    case InsightsRange::AllTime: break;
    }
    return QStringLiteral("all time");
}

QString insightsShareText(const InsightsSummary &summary, InsightsRange range)
{
    // The tiles' order: words, streak, then dictations and audio.
    QStringList lines{QStringLiteral("My Speecher stats, %1").arg(insightsPeriodName(range))};
    lines << QStringLiteral("%1 dictated").arg(plural(summary.words, QStringLiteral("word")));
    if (summary.words > 0) {
        lines << summary.bookComparison;
    }
    if (summary.currentStreak > 0 && summary.currentStreak >= summary.bestStreak) {
        lines << QStringLiteral("%1-day streak, my longest yet").arg(number(summary.currentStreak));
    } else if (summary.currentStreak > 0) {
        lines << QStringLiteral("%1-day streak (best: %2)")
                     .arg(number(summary.currentStreak), plural(summary.bestStreak, QStringLiteral("day")));
    }
    lines << QStringLiteral("%1, %2 of audio transcribed")
                 .arg(plural(summary.dictations, QStringLiteral("dictation")), audioTotalText(summary.audioMs));
    if (summary.wordsPerMinute > 0) {
        lines << QStringLiteral("%1 words per minute, %2× faster than typing")
                     .arg(number(summary.wordsPerMinute), speedup(summary));
    }
    QStringList apps;
    for (const AppShare &app : summary.apps) {
        apps << QStringLiteral("%1 %2%").arg(app.name).arg(app.percent);
    }
    if (!apps.isEmpty()) {
        lines << QStringLiteral("Top apps: %1").arg(apps.join(QStringLiteral(", ")));
    }
    return lines.join(u'\n');
}

QByteArray insightsJson(const InsightsSummary &summary, InsightsRange range, const QDate &today)
{
    QJsonArray apps;
    for (const AppShare &app : summary.apps) {
        apps.append(QJsonObject{{QStringLiteral("name"), app.name},
                                {QStringLiteral("words"), app.words},
                                {QStringLiteral("percent"), app.percent}});
    }
    QJsonArray days;
    for (const HeatmapDay &day : summary.heatmap) {
        if (day.dictations == 0) continue;
        days.append(QJsonObject{{QStringLiteral("date"), isoDate(day.date)},
                                {QStringLiteral("dictations"), day.dictations},
                                {QStringLiteral("words"), day.words},
                                {QStringLiteral("audioMs"), day.audioMs}});
    }
    const QJsonObject period{
        {QStringLiteral("name"), insightsPeriodName(range)},
        {QStringLiteral("words"), summary.words},
        {QStringLiteral("dictations"), summary.dictations},
        {QStringLiteral("audioMs"), summary.audioMs},
        {QStringLiteral("activeDays"), summary.activeDays},
        {QStringLiteral("averageAudioMs"), summary.averageAudioMs},
        {QStringLiteral("wordsPerMinute"), summary.wordsPerMinute},
        {QStringLiteral("typingWordsPerMinute"), summary.typingWordsPerMinute},
        {QStringLiteral("minutesSavedVersusTyping"), summary.minutesSavedVersusTyping},
        {QStringLiteral("bookComparison"), summary.bookComparison},
        {QStringLiteral("peakHour"), summary.hasHourData ? QJsonValue(summary.peakHour) : QJsonValue()},
        {QStringLiteral("apps"), apps},
    };
    const QJsonObject streak{
        {QStringLiteral("current"), summary.currentStreak},
        {QStringLiteral("best"), summary.bestStreak},
    };
    const QJsonObject records{
        {QStringLiteral("allTimeWords"), summary.allTimeWords},
        {QStringLiteral("firstDictation"), isoDate(summary.firstDictation)},
        {QStringLiteral("longestDictation"),
         QJsonObject{{QStringLiteral("date"), isoDate(summary.longest.date)},
                     {QStringLiteral("audioMs"), summary.longest.audioMs},
                     {QStringLiteral("words"), summary.longest.words},
                     {QStringLiteral("app"), summary.longest.appName}}},
        {QStringLiteral("busiestDay"),
         QJsonObject{{QStringLiteral("date"), isoDate(summary.busiestDay.date)},
                     {QStringLiteral("dictations"), summary.busiestDay.dictations}}},
        {QStringLiteral("wordiestDay"),
         QJsonObject{{QStringLiteral("date"), isoDate(summary.wordiestDay.date)},
                     {QStringLiteral("words"), summary.wordiestDay.words}}},
    };
    const QJsonObject root{
        {QStringLiteral("today"), isoDate(today)},
        {QStringLiteral("period"), period},
        {QStringLiteral("streak"), streak},
        {QStringLiteral("records"), records},
        {QStringLiteral("days"), days},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

QString insightsJsonFileName(const QDate &today)
{
    return QStringLiteral("speecher-stats-%1.json").arg(isoDate(today));
}

InsightsShareLabels insightsShareLabels()
{
    return {
        QStringLiteral("Share"),
        QStringLiteral("Copy image with stats"),
        QStringLiteral("Copy as text"),
        QStringLiteral("Save as JSON…"),
        QStringLiteral("Copied"),
        QStringLiteral("Saved"),
        QStringLiteral("Couldn't save"),
        QStringLiteral("Save stats"),
        QStringLiteral("JSON files"),
    };
}

QString insightsImageTitle()
{
    return QStringLiteral("My Speecher stats");
}

QString insightsRangeLabel(InsightsRange range)
{
    QString period = insightsPeriodName(range);
    period[0] = period.at(0).toUpper();
    return period;
}

QString insightsImagePaceLine(const InsightsSummary &summary)
{
    if (summary.wordsPerMinute <= 0) return {};
    return QStringLiteral("%1 · %2 words per minute").arg(summary.bookComparison, number(summary.wordsPerMinute));
}

} // namespace speecher
