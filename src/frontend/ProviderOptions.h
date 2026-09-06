#pragma once

#include "core/settings/SettingsSchema.h"
#include "providers/CliProxyCredentials.h"

namespace speecher {

inline QList<RowOption> cliproxyAccountOptions(const QString &type,
                                           const QString &selected,
                                           const QString &directory)
{
    const QList<CliProxyAccount> accounts = CliProxyCredentials::listAccounts(directory, type);
    QList<RowOption> options;
    // With several accounts and none chosen yet, force an explicit choice
    // instead of silently pinning whichever file sorts first.
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
        options.append({QString(), QStringLiteral("No accounts found"), directory, false});
    }
    return options;
}

} // namespace speecher
