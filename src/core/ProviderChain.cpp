#include "core/ProviderChain.h"

#include "core/AppSettings.h"

#include <QtGlobal>

namespace speecher {

bool isChainProviderId(ProviderRole role, const QString &id)
{
#ifdef SPEECHER_E2E_HOOKS
    // E2E-build-only hook, as in the provider codecs.
    if (id == QStringLiteral("e2e-stub") && qEnvironmentVariableIntValue("SPEECHER_E2E_STUB") == 1) {
        return true;
    }
#endif
    const std::optional<ProviderId> provider = parseProviderId(id);
    return provider && providerServes(*provider, role);
}

ServiceOrder normalizedServiceOrder(ProviderRole role, ServiceOrder services)
{
    services.order = serviceChain(role, services.order).mid(0, kMaxServices);
    for (auto it = services.choices.begin(); it != services.choices.end();) {
        if (!isChainProviderId(role, it.key())) {
            it = services.choices.erase(it);
            continue;
        }
        it->service = it.key();
        ++it;
    }
    return services;
}

QStringList serviceChain(ProviderRole role, const QStringList &order)
{
    QStringList chain;
    for (const QString &id : order) {
        if (!chain.contains(id) && isChainProviderId(role, id)) {
            chain.append(id);
        }
    }
    return chain;
}

QStringList serviceChain(const SpeechSettings &speech)
{
    return serviceChain(ProviderRole::Speech, speech.services.order);
}

QStringList serviceChain(const RefinementSettings &refinement)
{
    return refinement.enabled ? serviceChain(ProviderRole::Refinement, refinement.services.order) : QStringList();
}

} // namespace speecher
