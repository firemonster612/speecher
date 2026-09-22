#pragma once

#include "core/settings/SettingsSchema.h"
#include "providers/CliProxyCredentials.h"

#include <QHash>
#include <QString>
#include <QStringList>

namespace speecher {

class SettingsStore;

// The sign-in decisions every setup assistant shares, so the Qt, WinUI, and
// SwiftUI front ends render this logic instead of each owning a copy: which
// speech providers a CLI Proxy API account can back, whether usable accounts
// exist (the Welcome page's gate), and the opt-in to a CLI Proxy API account
// with a lossless way back to the sign-in it replaced.
class ProviderSignIn {
public:
    explicit ProviderSignIn(SettingsStore &settings);

    static bool supportsCliproxy(const QString &providerId);
    // Which CLI Proxy API account type backs a provider, speech ("codex",
    // "claude") or refinement ("openai", "anthropic"). The company's speech
    // and refinement sides share one account.
    static QString cliproxyAccountType(const QString &providerId);

    // The CLI Proxy API copy every assistant renders: the Welcome step's two
    // hints and the opt-in label the found-hint quotes. Shared because the
    // instruction names the control; a copy edited on one platform would send
    // readers to a checkbox that no longer says that.
    static QString cliproxyOptInLabel();
    static QString cliproxyAccountsFoundHint();
    static QString cliproxyAccountsMissingHint();

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

// The rows of a CLI Proxy API account picker, shared by the settings pages and
// the setup assistants on every platform: an explicit choice when several
// accounts exist and none is stored, expired/missing markers, disabled
// accounts kept visible but not selectable, and a disabled placeholder naming
// the directory when it holds nothing.
inline QList<RowOption> cliproxyAccountOptions(const QString &type,
                                               const QString &selected,
                                               const QString &directory)
{
    const QList<CliProxyAccount> accounts = CliProxyCredentials::listAccounts(directory, type);
    QList<RowOption> options;
    if (selected.isEmpty() && accounts.size() > 1) {
        options.append({QString(), QStringLiteral("Choose an account…")});
    }
    bool selectedFound = selected.isEmpty();
    for (const CliProxyAccount &account : accounts) {
        options.append({account.fileName,
                        account.expired ? account.label + QStringLiteral(" (expired)") : account.label,
                        account.disabled ? QStringLiteral("Disabled in CLI Proxy API") : QString(),
                        !account.disabled});
        selectedFound = selectedFound || account.fileName == selected;
    }
    // Keep a stored selection visible even if its file is currently missing.
    if (!selectedFound) {
        options.append({selected, selected + QStringLiteral(" (missing)")});
    }
    if (options.isEmpty()) {
        options.append({QString(),
                        QStringLiteral("No accounts found"),
                        QStringLiteral("Sign in with CLI Proxy API first; Speecher looks for its accounts in %1.")
                            .arg(directory),
                        false});
    }
    return options;
}

} // namespace speecher
