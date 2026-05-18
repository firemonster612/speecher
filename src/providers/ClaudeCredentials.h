#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

namespace speecher {

struct ClaudeCredentialResult {
    bool ok = false;
    QString accessToken;
    QString refreshToken;
    QDateTime expiresAt;
    QStringList scopes;
    QString subscriptionType;
    QString rateLimitTier;
    QString error;
};

class ClaudeCredentials {
public:
    static ClaudeCredentialResult load(const QString &path, bool refreshExpired = false);
};

} // namespace speecher
