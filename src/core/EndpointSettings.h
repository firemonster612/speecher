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

// Only engaged fields are user edits, including an explicitly emptied key.
struct RefinementEndpointEdit {
    std::optional<QString> format;
    std::optional<QString> baseUrl;
    std::optional<QString> apiKey;
    std::optional<QString> model;
    std::optional<QString> preset;
};
void editRefinementEndpoint(AppSettings &settings, const RefinementEndpointEdit &edit);

} // namespace speecher
