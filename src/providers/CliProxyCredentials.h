#pragma once

#include <QList>
#include <QString>

namespace speecher {

struct CliProxyAccount {
    QString fileName;
    QString label;
    bool disabled = false;
};

struct CliProxyCredentialResult {
    bool ok = false;
    QString accessToken;
    QString accountId;
    QString error;
};

class CliProxyCredentials {
public:
    static QList<CliProxyAccount> listAccounts(const QString &directory, const QString &type);
    static CliProxyCredentialResult load(const QString &directory, const QString &type, const QString &fileName);
};

} // namespace speecher
