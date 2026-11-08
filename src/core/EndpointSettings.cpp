#include "core/EndpointSettings.h"
#include "core/EndpointUrl.h"
#include "core/SecretStore.h"

namespace speecher {

RefinementEndpoint resolvedRefinementEndpoint(const RefinementSettings &refinement, const AccountSettings &accounts)
{
    return {refinement.server.format, refinement.server.baseUrl, refinementEndpointKey(refinement, accounts),
            refinement.services.choice(providerKey(ProviderId::Endpoint)).model};
}

QString refinementEndpointKey(const RefinementSettings &refinement, const AccountSettings &accounts)
{
    return usesProxyKey(refinement, accounts) ? accounts.cliproxyApiKey : refinement.server.apiKey;
}

bool usesProxyKey(const RefinementSettings &refinement, const AccountSettings &accounts)
{
    return refinement.server.apiKey.isEmpty() && sameOrigin(refinement.server.baseUrl, accounts.cliproxyBaseUrl);
}

void editRefinementEndpoint(AppSettings &settings, const RefinementEndpointEdit &edit)
{
    ServerSettings &server = settings.refinement.server;
    // A key belongs to its server. Moved to another one, the server has no
    // key until one is entered for it there; a first server keeps a key
    // entered before it.
    const bool movedServer = edit.baseUrl && !QUrl(server.baseUrl.trimmed()).host().isEmpty()
        && !sameOrigin(*edit.baseUrl, server.baseUrl);
    if (movedServer) {
        server.apiKey.clear();
        settings.unreadSecretKeys.removeAll(SecretStore::settingsKey(SecretStore::Secret::RefinementEndpointKey));
    }
    // Stored as typed: a field that is re-read while someone types must not
    // lose the "/" they just entered. Saving and requests drop trailing slashes.
    if (edit.baseUrl && edit.baseUrl->trimmed() != server.baseUrl) server.baseUrl = edit.baseUrl->trimmed();
    if (edit.format) server.format = *edit.format;
    if (edit.model) settings.refinement.services.choice(providerKey(ProviderId::Endpoint)).model = edit.model->trimmed();
}

void useCliproxyServer(AppSettings &settings)
{
    editRefinementEndpoint(settings, {.baseUrl = endpointApiBase(settings.accounts.cliproxyBaseUrl)});
    // The proxy's own server takes the proxy's key, never one entered for
    // another server at the same address.
    settings.refinement.server.apiKey.clear();
    settings.unreadSecretKeys.removeAll(SecretStore::settingsKey(SecretStore::Secret::RefinementEndpointKey));
}

} // namespace speecher
