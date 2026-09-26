#pragma once

#include "core/InsightsSummary.h"

#include <QByteArray>
#include <QDate>
#include <QString>

namespace speecher {

// Home's stats written for other people to read. Each covers the summary's
// period, plus the streak; the JSON also carries the records and every active
// day of the heatmap.

// A few lines for Slack or any chat: the totals, the pace, the streak and
// the top apps.
QString insightsShareText(const InsightsSummary &summary, InsightsRange range);
// One sentence that fits a 280-character post on X.
QString insightsPostText(const InsightsSummary &summary, InsightsRange range);
QByteArray insightsJson(const InsightsSummary &summary, InsightsRange range, const QDate &today);

} // namespace speecher
