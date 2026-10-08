#include "core/ProviderChain.h"

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
    static const QStringList speech{QStringLiteral("claude"), QStringLiteral("codex"),
                                    QStringLiteral("local"), QStringLiteral("endpoint")};
    static const QStringList refinement{QStringLiteral("openai"), QStringLiteral("anthropic"),
                                        QStringLiteral("local"), QStringLiteral("endpoint")};
    return (role == ProviderRole::Speech ? speech : refinement).contains(id);
}

QStringList normalizedFallbackProviders(ProviderRole role,
                                        const QString &primary,
                                        const QStringList &fallbacks)
{
    QStringList kept;
    if (role == ProviderRole::Refinement && primary == QStringLiteral("none")) {
        return kept;
    }
    for (const QString &id : fallbacks) {
        if (kept.size() == kMaxFallbackProviders) {
            break;
        }
        if (id != primary && !kept.contains(id) && isChainProviderId(role, id)) {
            kept.append(id);
        }
    }
    return kept;
}

QStringList providerChain(ProviderRole role, const QString &primary, const QStringList &fallbacks)
{
    if (role == ProviderRole::Refinement && primary == QStringLiteral("none")) {
        return {};
    }
    QStringList chain{primary};
    for (const QString &id : fallbacks) {
        if (!chain.contains(id) && isChainProviderId(role, id)) {
            chain.append(id);
        }
    }
    return chain;
}

} // namespace speecher
