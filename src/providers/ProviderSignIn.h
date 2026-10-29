#pragma once

#include "providers/CliProxyCredentials.h"

#include <QString>

namespace speecher {

class SettingsStore;

// The CLI Proxy API accounts the saved settings point at: the chosen account
// per type and the accounts saved in the resolved directory.
class ProviderSignIn {
public:
    explicit ProviderSignIn(SettingsStore &settings);

    // The account file chosen for a type ("codex", "claude"), or empty.
    QString chosenAccount(const QString &type) const;
    QString resolvedAccountDirectory() const;

    // CLI Proxy API's saved accounts of a type ("codex", "claude") in the
    // resolved directory, by file name, then the chosen one if its file is
    // gone, marked missing, so a picker keeps showing what was chosen.
    QList<CliProxyAccount> accounts(const QString &type) const;

private:
    SettingsStore &m_settings;
};

} // namespace speecher
