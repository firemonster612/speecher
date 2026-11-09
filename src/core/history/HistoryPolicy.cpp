#include "core/history/HistoryPolicy.h"

#include <algorithm>

namespace speecher {

HistoryKeep historyKeep(const HistorySettings &settings, const AppResolution &app, const Target &target)
{
    return std::min({settings.keeps, app.history.value_or(HistoryKeep::Audio),
                     target.secure ? HistoryKeep::Stats : HistoryKeep::Audio});
}

} // namespace speecher
