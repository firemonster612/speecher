#pragma once

#include "core/AppSettings.h"
#include <optional>

namespace speecher {

// The refinement server as a request reaches it: its format, API base, the
// key it sends and the "endpoint" entry's model.
struct RefinementEndpoint {
    QString format;
    QString apiBase;
    QString apiKey;
    QString model;
    bool operator==(const RefinementEndpoint &) const = default;
};

RefinementEndpoint resolvedRefinementEndpoint(const RefinementSettings &refinement, const AccountSettings &accounts);
// The key the refinement server sends: its own when it has one, else the CLI
// Proxy key while it is on the proxy's server (usesProxyKey), else none.
QString refinementEndpointKey(const RefinementSettings &refinement, const AccountSettings &accounts);
// Whether the refinement server, without a key of its own, sends the
// proxy's: it is on the proxy's server.
bool usesProxyKey(const RefinementSettings &refinement, const AccountSettings &accounts);

// Only engaged fields are user edits. The key is saved apart, through
// Accounts.
struct RefinementEndpointEdit {
    std::optional<QString> format;
    std::optional<QString> baseUrl;
    std::optional<QString> model;
};
void editRefinementEndpoint(AppSettings &settings, const RefinementEndpointEdit &edit);
// Points the refinement server at the CLI Proxy API server the sign-in
// names, with no key of its own, so it sends the proxy's.
void useCliproxyServer(AppSettings &settings);

} // namespace speecher
