#pragma once

#include "core/ProviderFailure.h"

#include <functional>
#include <optional>

namespace speecher {

// Readies a provider off the GUI thread: run() on a worker, then apply() back
// on the GUI thread with what run() returned. A preparation reports the
// failure that stopped it, or nothing once the provider is ready.
struct PreparationJob {
    bool showRefreshIndicator = false;
    std::function<std::optional<ProviderFailure>()> run;
    std::function<void(const std::optional<ProviderFailure> &)> apply;
};

} // namespace speecher
