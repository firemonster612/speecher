#pragma once

#include <QList>
#include <QString>

namespace speecher {

struct CliProxyAccount {
    QString fileName;
    QString label;
    bool disabled = false;
    bool expired = false;
};

struct CliProxyCredentialResult {
    bool ok = false;
    QString accessToken;
    QString accountId;
    QString error;
};

class CliProxyCredentials {
public:
    // Fast, file-only check: does the selected account's token need a refresh?
    static bool accountNeedsRefresh(const QString &directory, const QString &type, const QString &fileName);
    // Like load(), but an expired token is refreshed against the provider's
    // OAuth endpoint using the account's refresh_token, and the rotated tokens
    // are written back to the account file (refresh tokens rotate; keeping the
    // old file would strand CLI Proxy API's copy). Runs a bounded nested event
    // loop for the network call - worker threads only, never the GUI thread.
    static CliProxyCredentialResult loadWithRefresh(const QString &directory,
                                                    const QString &type,
                                                    const QString &fileName);
    static QList<CliProxyAccount> listAccounts(const QString &directory, const QString &type);
    static CliProxyCredentialResult load(const QString &directory, const QString &type, const QString &fileName);
};

} // namespace speecher
