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
    const bool serverChanged = edit.baseUrl && withoutTrailingSlashes(*edit.baseUrl) != effective.apiBase;
    const bool keyChanged = edit.apiKey && (edit.apiKey->trimmed() != effective.apiKey || unread);
    if ((serverChanged || keyChanged) && endpoint.preset == QStringLiteral("cliproxy")) {
        endpoint.preset.clear();
        endpoint.baseUrl = effective.apiBase;
        endpoint.apiKey = effective.apiKey;
        endpoint.useCliproxyKey = unread;
        if (unread) settings.unreadSecretKeys.append(endpointKey);
        else settings.unreadSecretKeys.removeAll(endpointKey);
    }
    if (serverChanged) endpoint.baseUrl = withoutTrailingSlashes(*edit.baseUrl);
    if (edit.apiKey) {
        if (keyChanged) {
            endpoint.apiKey = edit.apiKey->trimmed();
            endpoint.useCliproxyKey = false;
        }
        settings.unreadSecretKeys.removeAll(endpointKey);
    }
    if (edit.format) endpoint.format = *edit.format;
    if (edit.model) endpoint.model = edit.model->trimmed();
}

} // namespace speecher
