#include "core/InsightsExport.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace speecher {
namespace {

QString isoDate(const QDate &date)
{
    return date.toString(Qt::ISODate);
}

QString periodName(InsightsRange range)
{
    switch (range) {
    case InsightsRange::Last7Days: return QStringLiteral("last 7 days");
    case InsightsRange::Last30Days: return QStringLiteral("last 30 days");
    case InsightsRange::ThisYear: return QStringLiteral("this year");
    case InsightsRange::AllTime: break;
    }
    return QStringLiteral("all time");
}

} // namespace

QByteArray insightsJson(const InsightsSummary &summary, InsightsRange range, const QDate &today)
{
    QJsonArray apps;
    for (const AppShare &app : summary.apps) {
        QJsonObject entry{{QStringLiteral("words"), app.words}, {QStringLiteral("percent"), app.percent}};
        // The fold of the apps past the top five says how many it holds.
        if (app.otherApps) {
            entry.insert(QStringLiteral("otherApps"), app.otherApps);
        } else {
            entry.insert(QStringLiteral("name"), app.name);
        }
        apps.append(entry);
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
        {QStringLiteral("name"), periodName(range)},
        {QStringLiteral("words"), summary.words},
        {QStringLiteral("dictations"), summary.dictations},
        {QStringLiteral("audioMs"), summary.audioMs},
        {QStringLiteral("activeDays"), summary.activeDays},
        {QStringLiteral("averageAudioMs"), summary.averageAudioMs},
        {QStringLiteral("wordsPerMinute"), summary.wordsPerMinute},
        {QStringLiteral("typingWordsPerMinute"), summary.typingWordsPerMinute},
        {QStringLiteral("minutesSavedVersusTyping"), summary.minutesSavedVersusTyping},
        // { book, times }: the words are `times` the book's length.
        {QStringLiteral("bookComparison"),
         summary.bookComparison.book.isEmpty()
             ? QJsonValue()
             : QJsonObject{{QStringLiteral("book"), summary.bookComparison.book},
                           {QStringLiteral("times"), summary.bookComparison.times}}},
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

} // namespace speecher
