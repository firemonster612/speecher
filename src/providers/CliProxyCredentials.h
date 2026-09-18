#pragma once

#include <QList>
#include <QString>

class QNetworkRequest;

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

struct OauthRefreshResult {
    bool ok = false;
    QString accessToken;
    QString refreshToken;
    QString idToken;
    int expiresIn = 0;
    QString error;
};

class CliProxyCredentials {
public:
    // Standard OAuth refresh_token grant against the provider's token
    // endpoint. Bounded nested event loop - worker threads only.
    static OauthRefreshResult oauthRefresh(const QString &tokenUrl,
                                           const QString &clientId,
                                           const QString &refreshToken,
                                           const QString &scope = {},
                                           int timeoutMs = 10000);
    // Headers every token-endpoint request must carry. platform.claude.com
    // answers Qt's default "Mozilla/5.0" User-Agent with 429 rate_limit_error
    // before the grant is even looked at, so requests identify as the native
    // CLI's HTTP client instead.
    static void applyTokenRequestHeaders(QNetworkRequest &request);
    static QString claudeClientId();
    static QString codexClientId();
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
