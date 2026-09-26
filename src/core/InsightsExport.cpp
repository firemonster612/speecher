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

QString plural(int count, const QString &noun)
{
    return QStringLiteral("%1 %2").arg(number(count), count == 1 ? noun : noun + u's');
}

// "last 30 days", "this year", "all time".
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

// The period as the tail of a sentence: "in the last 30 days", "so far".
QString periodPhrase(InsightsRange range)
{
    switch (range) {
    case InsightsRange::Last7Days: return QStringLiteral("in the last 7 days");
    case InsightsRange::Last30Days: return QStringLiteral("in the last 30 days");
    case InsightsRange::ThisYear: return QStringLiteral("this year");
    case InsightsRange::AllTime: break;
    }
    return QStringLiteral("so far");
}

QString speedup(const InsightsSummary &summary)
{
    return QLocale().toString(double(summary.wordsPerMinute) / summary.typingWordsPerMinute, 'f', 1);
}

QString lowered(QString text)
{
    if (!text.isEmpty()) text[0] = text.at(0).toLower();
    return text;
}

QString isoDate(const QDate &date)
{
    return date.toString(Qt::ISODate);
}

} // namespace

QString insightsShareText(const InsightsSummary &summary, InsightsRange range)
{
    QStringList lines{QStringLiteral("My Speecher stats, %1").arg(periodName(range))};
    lines << QStringLiteral("%1 in %2 (%3 of audio)")
                 .arg(plural(summary.words, QStringLiteral("word")),
                      plural(summary.dictations, QStringLiteral("dictation")),
                      audioTotalText(summary.audioMs));
    if (summary.words > 0) {
        lines << summary.bookComparison;
    }
    if (summary.wordsPerMinute > 0) {
        lines << QStringLiteral("%1 words per minute, %2× faster than typing")
                     .arg(number(summary.wordsPerMinute), speedup(summary));
    }
    if (summary.currentStreak > 0 && summary.currentStreak >= summary.bestStreak) {
        lines << QStringLiteral("%1-day streak, my longest yet").arg(summary.currentStreak);
    } else if (summary.currentStreak > 0) {
        lines << QStringLiteral("%1-day streak (best: %2)")
                     .arg(summary.currentStreak)
                     .arg(plural(summary.bestStreak, QStringLiteral("day")));
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

QString insightsPostText(const InsightsSummary &summary, InsightsRange range)
{
    QString text = QStringLiteral("I've dictated %1 with Speecher %2")
                       .arg(plural(summary.words, QStringLiteral("word")), periodPhrase(range));
    if (summary.words > 0) {
        text += QStringLiteral(", %1").arg(lowered(summary.bookComparison));
    }
    if (summary.wordsPerMinute > 0) {
        text += QStringLiteral(", at %1 words per minute (%2× faster than typing)")
                    .arg(number(summary.wordsPerMinute), speedup(summary));
    }
    text += u'.';
    if (summary.currentStreak > 1) {
        text += QStringLiteral(" %1-day streak and counting.").arg(summary.currentStreak);
    }
    return text;
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
        {QStringLiteral("name"), periodName(range)},
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

} // namespace speecher
