#include "core/ProviderTable.h"

#include <algorithm>
#include <iterator>

namespace speecher {

namespace {

const struct ProviderInfo {
    ProviderId id;
    const char *key;
    bool speech;
    bool refinement;
    // Null for one that needs no sign-in.
    const char *account;
} kProviders[] = {
    {ProviderId::Claude, "claude", true, false, "Claude"},
    {ProviderId::Codex, "codex", true, false, "ChatGPT"},
    {ProviderId::OpenAi, "openai", false, true, "ChatGPT"},
    {ProviderId::Anthropic, "anthropic", false, true, "Claude"},
    {ProviderId::Local, "local", true, true, nullptr},
    {ProviderId::Endpoint, "endpoint", true, true, nullptr},
};

const ProviderInfo &info(ProviderId id)
{
    return *std::find_if(std::cbegin(kProviders), std::cend(kProviders),
                         [id](const ProviderInfo &provider) { return provider.id == id; });
}

} // namespace

std::optional<ProviderId> parseProviderId(const QString &key)
{
    const auto found = std::find_if(std::cbegin(kProviders), std::cend(kProviders), [&key](const ProviderInfo &provider) {
        return key == QLatin1String(provider.key);
    });
    if (found == std::cend(kProviders)) {
        return std::nullopt;
    }
    return found->id;
}

QString providerKey(ProviderId id)
{
    return QLatin1String(info(id).key);
}

bool providerServes(ProviderId id, ProviderRole role)
{
    return role == ProviderRole::Speech ? info(id).speech : info(id).refinement;
}

QString providerAccountName(ProviderId id)
{
    return QLatin1String(info(id).account);
}

bool servicePicksModel(ProviderRole role, const QString &providerKey)
{
    const std::optional<ProviderId> id = parseProviderId(providerKey);
    if (!id || !providerServes(*id, role)) {
        return false;
    }
    return role == ProviderRole::Refinement || id == ProviderId::Local || id == ProviderId::Endpoint;
}

} // namespace speecher
