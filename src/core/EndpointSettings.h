#pragma once

#include "core/AppSettings.h"
#include <optional>

namespace speecher {

// The refinement Custom Endpoint with its preset applied.
struct RefinementEndpoint {
    QString format;
    QString apiBase;
    QString apiKey;
    QString model;
    bool operator==(const RefinementEndpoint &) const = default;
};

RefinementEndpoint resolvedRefinementEndpoint(const RefinementSettings &settings);

// Only engaged fields are user edits. The key is saved apart, through
// Accounts, which also ends the use of the proxy's key (useCliproxyKey).
struct RefinementEndpointEdit {
    std::optional<QString> format;
    std::optional<QString> baseUrl;
    std::optional<QString> model;
    std::optional<QString> preset;
};
void editRefinementEndpoint(AppSettings &settings, const RefinementEndpointEdit &edit);

} // namespace speecher
