#include "providers/ClaudeCredentials.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace speecher {

ClaudeCredentialResult ClaudeCredentials::load(const QString &path)
{
    ClaudeCredentialResult result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("Claude credentials not found at %1; run `claude /login` or `claude auth login`").arg(path);
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        result.error = QStringLiteral("Claude credentials file is not valid JSON");
        return result;
    }

    const QJsonObject oauth = doc.object().value(QStringLiteral("claudeAiOauth")).toObject();
    result.accessToken = oauth.value(QStringLiteral("accessToken")).toString();
    result.refreshToken = oauth.value(QStringLiteral("refreshToken")).toString();
    result.subscriptionType = oauth.value(QStringLiteral("subscriptionType")).toString();
    result.rateLimitTier = oauth.value(QStringLiteral("rateLimitTier")).toString();
    const qint64 expires = static_cast<qint64>(oauth.value(QStringLiteral("expiresAt")).toDouble());
    result.expiresAt = QDateTime::fromSecsSinceEpoch(expires / (expires > 9999999999LL ? 1000 : 1), Qt::UTC);
    for (const QJsonValue &scope : oauth.value(QStringLiteral("scopes")).toArray()) {
        result.scopes << scope.toString();
    }

    if (result.accessToken.isEmpty()) {
        result.error = QStringLiteral("Claude credentials do not contain claudeAiOauth.accessToken");
        return result;
    }
    if (result.expiresAt.isValid() && result.expiresAt <= QDateTime::currentDateTimeUtc()) {
        result.error = QStringLiteral("Claude login expired; run `claude /login` or `claude auth login`");
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace speecher
