#include "providers/ProviderSignIn.h"

#include "core/SettingsStore.h"

namespace speecher {

namespace {

const QString kCliproxyMode = QStringLiteral("cliproxy");

QString signInMode(const SettingsStore &settings, const QString &providerId)
{
    return providerId == QStringLiteral("codex") ? settings.openAiAuthMode()
                                                 : settings.anthropicAuthMode();
}

void setSignInMode(SettingsStore &settings, const QString &providerId, const QString &mode)
{
    if (providerId == QStringLiteral("codex")) {
        settings.setOpenAiAuthMode(mode);
    } else {
        settings.setAnthropicAuthMode(mode);
    }
}

QString defaultSignInMode(const QString &providerId)
{
    return providerId == QStringLiteral("codex") ? QStringLiteral("auto")
                                                 : QStringLiteral("oauth");
}

} // namespace

ProviderSignIn::ProviderSignIn(SettingsStore &settings)
    : m_settings(settings)
{
}

bool ProviderSignIn::supportsCliproxy(const QString &providerId)
{
    return providerId == QStringLiteral("claude") || providerId == QStringLiteral("codex");
}

QString ProviderSignIn::cliproxyAccountType(const QString &providerId)
{
    return providerId == QStringLiteral("codex") ? QStringLiteral("codex")
                                                 : QStringLiteral("claude");
}

bool ProviderSignIn::usingCliproxy(const QString &providerId) const
{
    return signInMode(m_settings, providerId) == kCliproxyMode;
}

void ProviderSignIn::setUseCliproxy(const QString &providerId, bool use)
{
    const QString current = signInMode(m_settings, providerId);
    if (use) {
        if (current != kCliproxyMode && !m_fallbackModes.contains(providerId)) {
            m_fallbackModes.insert(providerId, current);
        }
        setSignInMode(m_settings, providerId, kCliproxyMode);
        return;
    }
    setSignInMode(m_settings, providerId,
                  m_fallbackModes.value(providerId, defaultSignInMode(providerId)));
}

QString ProviderSignIn::cliproxyAccount(const QString &providerId) const
{
    return providerId == QStringLiteral("codex") ? m_settings.openAiCliproxyAccount()
                                                 : m_settings.anthropicCliproxyAccount();
}

void ProviderSignIn::setCliproxyAccount(const QString &providerId, const QString &account)
{
    if (providerId == QStringLiteral("codex")) {
        m_settings.setOpenAiCliproxyAccount(account);
    } else {
        m_settings.setAnthropicCliproxyAccount(account);
    }
}

QString ProviderSignIn::configuredAccountDirectory() const
{
    return m_settings.configuredCliproxyOauthDir();
}

void ProviderSignIn::setAccountDirectory(const QString &directory)
{
    m_settings.setCliproxyOauthDir(directory);
}

QString ProviderSignIn::resolvedAccountDirectory() const
{
    return m_settings.cliproxyOauthDir();
}

bool ProviderSignIn::anyUsableAccount(const QStringList &providerIds) const
{
    const QString directory = resolvedAccountDirectory();
    for (const QString &providerId : providerIds) {
        if (!supportsCliproxy(providerId)) {
            continue;
        }
        const QList<CliProxyAccount> accounts =
            CliProxyCredentials::listAccounts(directory, cliproxyAccountType(providerId));
        for (const CliProxyAccount &account : accounts) {
            if (!account.disabled) {
                return true;
            }
        }
    }
    return false;
}

} // namespace speecher
