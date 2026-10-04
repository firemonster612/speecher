#pragma once

#include "core/ProviderFailure.h"

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
    // Why there is no usable token: a missing or expired sign-in unless a
    // refresh said otherwise.
    ProviderFailureKind kind = ProviderFailureKind::Unavailable;
    int httpStatus = 0;
};

class ClaudeCredentials {
public:
    static ClaudeCredentialResult load(const QString &path, bool refreshExpired = false);
    static bool requiresRefresh(const QString &path);
    static QString installedVersion();
};

} // namespace speecher
