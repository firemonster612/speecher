#pragma once

#include "core/InsightsSummary.h"

#include <QByteArray>
#include <QDate>
#include <QString>

namespace speecher {

// Home's stats written for other people to read. Each covers the summary's
// period, plus the streak; the JSON also carries the records and every active
// day of the heatmap.

// "last 30 days", "this year", "all time".
QString insightsPeriodName(InsightsRange range);
// A few lines for Slack or any chat: the totals, the pace, the streak and
// the top apps.
QString insightsShareText(const InsightsSummary &summary, InsightsRange range);
QByteArray insightsJson(const InsightsSummary &summary, InsightsRange range, const QDate &today);
// "speecher-stats-2026-09-26.json", the name a save dialog suggests.
QString insightsJsonFileName(const QDate &today);

// What the Share menu and its confirmations say.
struct InsightsShareLabels {
    QString share;       // "Share"
    QString copyImage;   // "Copy image with stats"
    QString copyText;    // "Copy as text"
    QString saveJson;    // "Save as JSON…"
    QString copied;      // "Copied"
    QString saved;       // "Saved"
    QString saveFailed;  // "Couldn't save"
    QString saveTitle;   // the save dialog's title
    QString jsonFilter;  // "JSON files"
};
InsightsShareLabels insightsShareLabels();

// The shared image's heading and the line under its figures.
QString insightsImageTitle();
// "Last 30 days": the period picker's choice and the image's period line.
QString insightsRangeLabel(InsightsRange range);
// "About half of Hamlet · 142 words per minute"; empty with no audio.
QString insightsImagePaceLine(const InsightsSummary &summary);

} // namespace speecher
