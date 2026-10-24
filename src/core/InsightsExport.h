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

} // namespace speecher
