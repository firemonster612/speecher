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
// The key a Custom endpoint sends: its own when it has one, else the CLI
// Proxy key while it is on the proxy's server (usesProxyKey), else none.
QString refinementEndpointKey(const RefinementSettings &settings);
// Whether a Custom endpoint without a key of its own sends the proxy's: it
// is on the proxy's server.
bool usesProxyKey(const RefinementSettings &settings);
// Turns the CLI Proxy preset into a Custom endpoint at the proxy's address,
// with no key of its own.
void detachRefinementEndpoint(AppSettings &settings);

// Only engaged fields are user edits. The key is saved apart, through
// Accounts.
struct RefinementEndpointEdit {
    std::optional<QString> format;
    std::optional<QString> baseUrl;
    std::optional<QString> model;
    std::optional<QString> preset;
};
void editRefinementEndpoint(AppSettings &settings, const RefinementEndpointEdit &edit);

} // namespace speecher
