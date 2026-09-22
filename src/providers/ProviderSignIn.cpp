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
    const bool openAiFamily =
        providerId == QStringLiteral("codex") || providerId == QStringLiteral("openai");
    return openAiFamily ? QStringLiteral("codex") : QStringLiteral("claude");
}

QString ProviderSignIn::cliproxyOptInLabel()
{
    return QStringLiteral("Use a CLI Proxy API account instead of the service's own sign-in");
}

QString ProviderSignIn::cliproxyAccountsFoundHint()
{
    return QStringLiteral(
        "To use one of these accounts, turn on \"Use a CLI Proxy API account\" on the "
        "Transcription step.");
}

QString ProviderSignIn::cliproxyAccountsMissingHint()
{
    return QStringLiteral(
        "Optional: Claude and Codex accounts saved by CLI Proxy API also work. If yours live in "
        "a custom directory, enter it below.");
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
