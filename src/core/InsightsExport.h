#pragma once

#include "core/InsightsSummary.h"

#include <QByteArray>
#include <QDate>

namespace speecher {

// Home's stats as a JSON file: the summary's period, the streak, the records
// and every active day of the heatmap. The period's name ("last 30 days") is
// part of the file format; Home words everything else.
QByteArray insightsJson(const InsightsSummary &summary, InsightsRange range, const QDate &today);

} // namespace speecher
