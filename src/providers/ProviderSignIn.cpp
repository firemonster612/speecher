#include "providers/ProviderSignIn.h"

#include "core/SettingsStore.h"

#include <algorithm>

namespace speecher {

ProviderSignIn::ProviderSignIn(SettingsStore &settings)
    : m_settings(settings)
{
}

QString ProviderSignIn::chosenAccount(const QString &type) const
{
    return type == QStringLiteral("codex") ? m_settings.openAiCliproxyAccount()
                                           : m_settings.anthropicCliproxyAccount();
}

QString ProviderSignIn::resolvedAccountDirectory() const
{
    return m_settings.cliproxyOauthDir();
}

QList<CliProxyAccount> ProviderSignIn::accounts(const QString &type) const
{
    QList<CliProxyAccount> accounts = CliProxyCredentials::listAccounts(resolvedAccountDirectory(), type);
    const QString chosen = chosenAccount(type);
    const bool found = std::any_of(accounts.cbegin(), accounts.cend(),
                                   [&chosen](const CliProxyAccount &account) { return account.fileName == chosen; });
    if (!chosen.isEmpty() && !found) {
        accounts.append({.fileName = chosen, .label = chosen, .missing = true});
    }
    return accounts;
}

} // namespace speecher
