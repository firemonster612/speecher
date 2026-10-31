#include "core/EndpointSettings.h"
#include "core/EndpointUrl.h"
#include "core/SecretStore.h"

namespace speecher {

RefinementEndpoint resolvedRefinementEndpoint(const RefinementSettings &settings)
{
    const RefinementEndpointSettings &endpoint = settings.endpoint;
    if (endpoint.preset == QStringLiteral("cliproxy")) {
        return {endpoint.format, endpointApiBase(settings.cliproxyBaseUrl), settings.cliproxyApiKey,
                endpoint.model};
    }
    return {endpoint.format, endpoint.baseUrl, refinementEndpointKey(settings), endpoint.model};
}

QString refinementEndpointKey(const RefinementSettings &settings)
{
    if (!settings.endpoint.apiKey.isEmpty()) {
        return settings.endpoint.apiKey;
    }
    return usesProxyKey(settings) ? settings.cliproxyApiKey : QString();
}

bool usesProxyKey(const RefinementSettings &settings)
{
    return settings.endpoint.apiKey.isEmpty() && sameOrigin(settings.endpoint.baseUrl, settings.cliproxyBaseUrl);
}

void detachRefinementEndpoint(AppSettings &settings)
{
    auto &endpoint = settings.refinement.endpoint;
    endpoint.baseUrl = resolvedRefinementEndpoint(settings.refinement).apiBase;
    endpoint.preset.clear();
    endpoint.apiKey.clear();
    settings.unreadSecretKeys.removeAll(SecretStore::settingsKey(SecretStore::Secret::RefinementEndpointKey));
}

void editRefinementEndpoint(AppSettings &settings, const RefinementEndpointEdit &edit)
{
    auto &endpoint = settings.refinement.endpoint;
    const auto effective = resolvedRefinementEndpoint(settings.refinement);
    // Another address, as opposed to the same one typed with a trailing slash.
    const bool urlChanged = edit.baseUrl && withoutTrailingSlashes(*edit.baseUrl) != withoutTrailingSlashes(effective.apiBase);
    const bool urlEdited = edit.baseUrl && edit.baseUrl->trimmed() != effective.apiBase;
    const bool leavingPreset = edit.preset && *edit.preset != QStringLiteral("cliproxy");
    // The preset never overwrites the Custom fields, so a Custom server entered
    // before comes back with its own key; only without one does Custom start
    // from the proxy's address.
    const bool customSaved = !endpoint.baseUrl.isEmpty();
    if ((urlChanged || (leavingPreset && !customSaved)) && endpoint.preset == QStringLiteral("cliproxy")) {
        detachRefinementEndpoint(settings);
    }
    // A key belongs to its server. Moved to another one, the endpoint has no
    // key until one is entered for it there; a first server keeps a key
    // entered before it.
    const bool movedServer = edit.baseUrl && !QUrl(effective.apiBase.trimmed()).host().isEmpty()
        && !sameOrigin(*edit.baseUrl, effective.apiBase);
    if (movedServer) {
        endpoint.apiKey.clear();
        settings.unreadSecretKeys.removeAll(SecretStore::settingsKey(SecretStore::Secret::RefinementEndpointKey));
    }
    // Stored as typed: a field that is re-read while someone types must not
    // lose the "/" they just entered. Saving and requests drop trailing slashes.
    if (urlEdited) endpoint.baseUrl = edit.baseUrl->trimmed();
    if (edit.preset) endpoint.preset = *edit.preset;
    if (edit.format) endpoint.format = *edit.format;
    if (edit.model) endpoint.model = edit.model->trimmed();
}

} // namespace speecher
