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
    return {endpoint.format, endpoint.baseUrl,
            endpoint.useCliproxyKey ? settings.cliproxyApiKey : endpoint.apiKey, endpoint.model};
}

void editRefinementEndpoint(AppSettings &settings, const RefinementEndpointEdit &edit)
{
    auto &endpoint = settings.refinement.endpoint;
    const auto effective = resolvedRefinementEndpoint(settings.refinement);
    const auto proxyKey = SecretStore::settingsKey(SecretStore::Secret::CliproxyApiKey);
    const auto endpointKey = SecretStore::settingsKey(SecretStore::Secret::RefinementEndpointKey);
    const bool proxy = endpoint.preset == QStringLiteral("cliproxy") || endpoint.useCliproxyKey;
    const bool unread = settings.unreadSecretKeys.contains(proxy ? proxyKey : endpointKey);
    // Another server, as opposed to the same address typed with a trailing slash.
    const bool serverChanged = edit.baseUrl && withoutTrailingSlashes(*edit.baseUrl) != withoutTrailingSlashes(effective.apiBase);
    const bool urlEdited = edit.baseUrl && edit.baseUrl->trimmed() != effective.apiBase;
    const bool keyChanged = edit.apiKey && (edit.apiKey->trimmed() != effective.apiKey || unread);
    const bool leavingPreset = edit.preset && *edit.preset != QStringLiteral("cliproxy");
    if ((serverChanged || keyChanged || leavingPreset) && endpoint.preset == QStringLiteral("cliproxy")) {
        endpoint.preset.clear();
        endpoint.baseUrl = effective.apiBase;
        // The proxy's key may follow only to the proxy's own address; a new
        // server gets it only if this same edit supplies it.
        const bool sameServer = !serverChanged;
        endpoint.apiKey = sameServer || edit.apiKey ? effective.apiKey : QString();
        endpoint.useCliproxyKey = sameServer && unread;
        settings.unreadSecretKeys.removeAll(endpointKey);
    }
    // Stored as typed: a field that is re-read while someone types must not
    // lose the "/" they just entered. Saving and requests drop trailing slashes.
    if (urlEdited) endpoint.baseUrl = edit.baseUrl->trimmed();
    if (edit.apiKey) {
        if (keyChanged) {
            endpoint.apiKey = edit.apiKey->trimmed();
            endpoint.useCliproxyKey = false;
        }
        settings.unreadSecretKeys.removeAll(endpointKey);
    }
    if (edit.preset) endpoint.preset = *edit.preset;
    if (edit.format) endpoint.format = *edit.format;
    if (edit.model) endpoint.model = edit.model->trimmed();
}

} // namespace speecher
