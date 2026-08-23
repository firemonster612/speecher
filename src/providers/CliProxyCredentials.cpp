#include "providers/CliProxyCredentials.h"

#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>

namespace speecher {
namespace {

// The same public OAuth clients the CLIs themselves use; only the
// refresh_token grant is exercised, never a login.
constexpr auto claudeOauthClientId = "9d1c250a-e61b-44d9-88ed-5944d1962f5e";
constexpr auto codexOauthClientId = "app_EMoamEEZ73f0CkXaXp7hrann";
constexpr int accountLockTimeoutMs = 1000;

QString refreshTokenUrl(const QString &type)
{
    if (type == QStringLiteral("claude")) {
        const QString override = qEnvironmentVariable("SPEECHER_CLIPROXY_CLAUDE_TOKEN_URL");
        return override.isEmpty() ? QStringLiteral("https://platform.claude.com/v1/oauth/token") : override;
    }
    const QString override = qEnvironmentVariable("SPEECHER_CLIPROXY_CODEX_TOKEN_URL");
    return override.isEmpty() ? QStringLiteral("https://auth.openai.com/oauth/token") : override;
}

QJsonObject readAccountObject(const QString &directory, const QString &fileName)
{
    QFile file(QDir(directory).filePath(fileName));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

QString accountLabel(const QString &fileName, const QString &type)
{
    QString label = fileName;
    label.remove(0, type.size() + 1);
    label.chop(QStringLiteral(".json").size());
    return label;
}

bool accountExpired(const QJsonObject &account)
{
    const QDateTime expiry = QDateTime::fromString(account.value(QStringLiteral("expired")).toString(), Qt::ISODate);
    return expiry.isValid() && expiry <= QDateTime::currentDateTimeUtc();
}

QString accountValidationError(const QString &directory,
                               const QString &type,
                               const QString &fileName,
                               const QJsonObject &account)
{
    if (account.isEmpty()) {
        return QStringLiteral("Could not read CLI Proxy API account %1 in %2").arg(fileName, directory);
    }
    if (account.value(QStringLiteral("type")).toString() != type) {
        return QStringLiteral("CLI Proxy API account %1 is not a %2 account").arg(fileName, type);
    }
    if (account.value(QStringLiteral("disabled")).toBool()) {
        return QStringLiteral("CLI Proxy API account %1 is disabled").arg(fileName);
    }
    if (account.value(QStringLiteral("access_token")).toString().trimmed().isEmpty()) {
        return QStringLiteral("No access token in CLI Proxy API account %1").arg(fileName);
    }
    const QString expiredValue = account.value(QStringLiteral("expired")).toString();
    if (!expiredValue.isEmpty() && !QDateTime::fromString(expiredValue, Qt::ISODate).isValid()) {
        return QStringLiteral("Could not parse expiry \"%1\" in CLI Proxy API account %2").arg(expiredValue, fileName);
    }
    return {};
}

QString resolveAccountFileName(const QString &directory,
                               const QString &type,
                               const QString &fileName,
                               QString *error)
{
    if (!fileName.isEmpty()) {
        return fileName;
    }
    const QList<CliProxyAccount> accounts = CliProxyCredentials::listAccounts(directory, type);
    if (accounts.size() == 1) {
        return accounts.first().fileName;
    }
    if (error) {
        *error = accounts.isEmpty()
            ? QStringLiteral("No CLI Proxy API %1 accounts found in %2").arg(type, directory)
            : QStringLiteral("Multiple CLI Proxy API %1 accounts found; choose one in provider settings").arg(type);
    }
    return {};
}

bool refreshAccountFile(const QString &directory,
                        const QString &type,
                        const QString &fileName,
                        const QJsonObject &account,
                        QString *error)
{
    const QString refreshToken = account.value(QStringLiteral("refresh_token")).toString().trimmed();
    if (refreshToken.isEmpty()) {
        if (error) {
            *error = QStringLiteral("CLI Proxy API account %1 has no refresh token; sign in again through CLI Proxy API").arg(fileName);
        }
        return false;
    }

    const OauthRefreshResult refreshed = CliProxyCredentials::oauthRefresh(
        refreshTokenUrl(type),
        type == QStringLiteral("claude") ? CliProxyCredentials::claudeClientId()
                                         : CliProxyCredentials::codexClientId(),
        refreshToken,
        type == QStringLiteral("codex") ? QStringLiteral("openid profile email") : QString());
    if (!refreshed.ok) {
        if (error) {
            *error = QStringLiteral("Could not refresh the CLI Proxy API %1 token: %2").arg(type, refreshed.error);
        }
        return false;
    }

    QJsonObject updated = account;
    updated.insert(QStringLiteral("access_token"), refreshed.accessToken);
    if (!refreshed.refreshToken.isEmpty()) {
        updated.insert(QStringLiteral("refresh_token"), refreshed.refreshToken);
    }
    if (!refreshed.idToken.isEmpty()) {
        updated.insert(QStringLiteral("id_token"), refreshed.idToken);
    }
    const QDateTime now = QDateTime::currentDateTimeUtc();
    updated.insert(QStringLiteral("expired"), now.addSecs(refreshed.expiresIn).toString(Qt::ISODate));
    updated.insert(QStringLiteral("last_refresh"), now.toString(Qt::ISODate));

    QSaveFile file(QDir(directory).filePath(fileName));
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = QStringLiteral("Could not write refreshed CLI Proxy API account %1").arg(fileName);
        }
        return false;
    }
    file.write(QJsonDocument(updated).toJson());
    if (!file.commit()) {
        if (error) {
            *error = QStringLiteral("Could not write refreshed CLI Proxy API account %1").arg(fileName);
        }
        return false;
    }
    return true;
}

} // namespace

QString CliProxyCredentials::claudeClientId()
{
    return QString::fromLatin1(claudeOauthClientId);
}

QString CliProxyCredentials::codexClientId()
{
    return QString::fromLatin1(codexOauthClientId);
}

OauthRefreshResult CliProxyCredentials::oauthRefresh(const QString &tokenUrl,
                                                     const QString &clientId,
                                                     const QString &refreshToken,
                                                     const QString &scope,
                                                     int timeoutMs)
{
    OauthRefreshResult result;
    QJsonObject body{
        {QStringLiteral("grant_type"), QStringLiteral("refresh_token")},
        {QStringLiteral("refresh_token"), refreshToken},
        {QStringLiteral("client_id"), clientId},
    };
    if (!scope.isEmpty()) {
        body.insert(QStringLiteral("scope"), scope);
    }

    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(tokenUrl)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *reply = manager.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QEventLoop loop;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
    watchdog.start(timeoutMs);
    loop.exec();
    const bool timedOut = !reply->isFinished();
    if (timedOut) {
        reply->abort();
    }
    reply->deleteLater();
    if (timedOut) {
        result.error = QStringLiteral("the token endpoint timed out");
        return result;
    }
    const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
    result.accessToken = response.value(QStringLiteral("access_token")).toString().trimmed();
    if (reply->error() != QNetworkReply::NoError || result.accessToken.isEmpty()) {
        const QJsonValue errorValue = response.value(QStringLiteral("error"));
        const QString detail = errorValue.isObject()
            ? errorValue.toObject().value(QStringLiteral("message")).toString()
            : errorValue.toString();
        result.error = detail.isEmpty() ? QStringLiteral("the token endpoint rejected the refresh") : detail;
        return result;
    }
    result.refreshToken = response.value(QStringLiteral("refresh_token")).toString().trimmed();
    result.idToken = response.value(QStringLiteral("id_token")).toString().trimmed();
    bool expiresInValid = false;
    const int expiresIn = response.value(QStringLiteral("expires_in")).toVariant().toInt(&expiresInValid);
    result.expiresIn = expiresInValid ? expiresIn : 3600;
    result.ok = true;
    return result;
}

bool CliProxyCredentials::accountNeedsRefresh(const QString &directory, const QString &type, const QString &fileName)
{
    const QString resolved = resolveAccountFileName(directory, type, fileName, nullptr);
    if (resolved.isEmpty()) {
        return false;
    }
    return accountExpired(readAccountObject(directory, resolved));
}

CliProxyCredentialResult CliProxyCredentials::loadWithRefresh(const QString &directory,
                                                              const QString &type,
                                                              const QString &fileName)
{
    QString resolveError;
    const QString resolved = resolveAccountFileName(directory, type, fileName, &resolveError);
    if (resolved.isEmpty()) {
        return {false, {}, {}, resolveError};
    }
    QJsonObject account = readAccountObject(directory, resolved);
    const QString initialError = accountValidationError(directory, type, resolved, account);
    if (!initialError.isEmpty()) {
        return {false, {}, {}, initialError};
    }
    if (!accountExpired(account)) {
        return {true,
                account.value(QStringLiteral("access_token")).toString().trimmed(),
                account.value(QStringLiteral("account_id")).toString(),
                {}};
    }

    QLockFile lock(QDir(directory).filePath(resolved) + QStringLiteral(".lock"));
    if (!lock.tryLock(accountLockTimeoutMs)) {
        return {false, {}, {}, QStringLiteral("Could not lock CLI Proxy API account %1 for refresh").arg(resolved)};
    }
    account = readAccountObject(directory, resolved);
    const QString lockedError = accountValidationError(directory, type, resolved, account);
    if (!lockedError.isEmpty()) {
        return {false, {}, {}, lockedError};
    }
    if (!accountExpired(account)) {
        return {true,
                account.value(QStringLiteral("access_token")).toString().trimmed(),
                account.value(QStringLiteral("account_id")).toString(),
                {}};
    }
    QString refreshError;
    if (!refreshAccountFile(directory, type, resolved, account, &refreshError)) {
        return {false, {}, {}, refreshError};
    }
    return load(directory, type, resolved);
}

QList<CliProxyAccount> CliProxyCredentials::listAccounts(const QString &directory, const QString &type)
{
    QList<CliProxyAccount> accounts;
    const QStringList fileNames = QDir(directory).entryList({type + QStringLiteral("-*.json")}, QDir::Files, QDir::Name);
    for (const QString &fileName : fileNames) {
        const QJsonObject account = readAccountObject(directory, fileName);
        if (account.value(QStringLiteral("type")).toString() != type) {
            continue;
        }
        accounts.append({fileName, accountLabel(fileName, type), account.value(QStringLiteral("disabled")).toBool(),
                         accountExpired(account)});
    }
    return accounts;
}

CliProxyCredentialResult CliProxyCredentials::load(const QString &directory, const QString &type, const QString &fileName)
{
    QString resolvedFileName = fileName;
    if (resolvedFileName.isEmpty()) {
        const QList<CliProxyAccount> accounts = listAccounts(directory, type);
        if (accounts.isEmpty()) {
            return {false, {}, {}, QStringLiteral("No CLI Proxy API %1 accounts found in %2").arg(type, directory)};
        }
        if (accounts.size() > 1) {
            return {false, {}, {},
                    QStringLiteral("Multiple CLI Proxy API %1 accounts found; choose one in provider settings").arg(type)};
        }
        resolvedFileName = accounts.first().fileName;
    }

    const QJsonObject account = readAccountObject(directory, resolvedFileName);
    const QString validationError = accountValidationError(directory, type, resolvedFileName, account);
    if (!validationError.isEmpty()) {
        return {false, {}, {}, validationError};
    }
    // CLI Proxy API and Speecher may both refresh these rotating tokens. The
    // refresh path serializes its read-network-write sequence with the account lock.
    const QString expiredValue = account.value(QStringLiteral("expired")).toString();
    const QDateTime expiry = QDateTime::fromString(expiredValue, Qt::ISODate);
    if (expiry.isValid() && expiry <= QDateTime::currentDateTimeUtc()) {
        return {false, {}, {},
                QStringLiteral("CLI Proxy API token for %1 is expired; run CLI Proxy API to refresh it").arg(resolvedFileName)};
    }
    return {true,
            account.value(QStringLiteral("access_token")).toString().trimmed(),
            account.value(QStringLiteral("account_id")).toString(),
            {}};
}

} // namespace speecher
