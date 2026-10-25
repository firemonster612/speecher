#pragma once

#include "providers/CliProxyCredentials.h"

#include <QHash>
#include <QString>
#include <QStringList>

namespace speecher {

class SettingsStore;

// The sign-in decisions setup makes: which speech providers a CLI Proxy API
// account can back, whether usable accounts exist (the Welcome page's gate),
// and the opt-in to a CLI Proxy API account with a lossless way back to the
// sign-in it replaced.
class ProviderSignIn {
public:
    explicit ProviderSignIn(SettingsStore &settings);

    static bool supportsCliproxy(const QString &providerId);
    // Which CLI Proxy API account type backs a provider, speech ("codex",
    // "claude") or refinement ("openai", "anthropic"). The company's speech
    // and refinement sides share one account.
    static QString cliproxyAccountType(const QString &providerId);

    bool usingCliproxy(const QString &providerId) const;
    // Opting out returns to the mode this model first saw the provider leave,
    // so a sign-in chosen in Settings (an OpenAI API key, say) survives a
    // round trip through CLI Proxy API instead of being rewritten to the
    // default. The memory lives with this instance, so the round trip must
    // stay within it; opting out from a later run falls back to the
    // provider's default mode. The transcription providers share one sign-in
    // setting per company with refinement, so this writes the same modes the
    // Providers settings page does.
    void setUseCliproxy(const QString &providerId, bool use);

    QString cliproxyAccount(const QString &providerId) const;
    void setCliproxyAccount(const QString &providerId, const QString &account);

    // Empty means automatic detection.
    QString configuredAccountDirectory() const;
    void setAccountDirectory(const QString &directory);
    QString resolvedAccountDirectory() const;

    // Whether any enabled CLI Proxy API account exists for one of the given
    // speech providers. A disabled account or one for a service the build
    // lacks cannot dictate, so it must not open the Welcome gate.
    bool anyUsableAccount(const QStringList &providerIds) const;

private:
    SettingsStore &m_settings;
    QHash<QString, QString> m_fallbackModes;
};

} // namespace speecher
