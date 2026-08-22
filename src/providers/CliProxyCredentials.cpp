#include "providers/CliProxyCredentials.h"

#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
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
                        QString *error)
{
    const QJsonObject account = readAccountObject(directory, fileName);
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
        refreshToken);
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
    const QDateTime now = QDateTime::currentDateTime();
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
                                                     int timeoutMs)
{
    OauthRefreshResult result;
    const QJsonObject body{
        {QStringLiteral("grant_type"), QStringLiteral("refresh_token")},
        {QStringLiteral("refresh_token"), refreshToken},
        {QStringLiteral("client_id"), clientId},
    };

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
    const bool timedOut = !watchdog.isActive();
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
        const QString detail = response.value(QStringLiteral("error")).toString();
        result.error = detail.isEmpty() ? QStringLiteral("the token endpoint rejected the refresh") : detail;
        return result;
    }
    result.refreshToken = response.value(QStringLiteral("refresh_token")).toString().trimmed();
    result.idToken = response.value(QStringLiteral("id_token")).toString().trimmed();
    result.expiresIn = response.value(QStringLiteral("expires_in")).toInt(3600);
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
    if (accountExpired(readAccountObject(directory, resolved))) {
        QString refreshError;
        if (!refreshAccountFile(directory, type, resolved, &refreshError)) {
            return {false, {}, {}, refreshError};
        }
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
    if (account.isEmpty()) {
        return {false, {}, {}, QStringLiteral("Could not read CLI Proxy API account %1 in %2").arg(resolvedFileName, directory)};
    }
    if (account.value(QStringLiteral("type")).toString() != type) {
        return {false, {}, {}, QStringLiteral("CLI Proxy API account %1 is not a %2 account").arg(resolvedFileName, type)};
    }
    if (account.value(QStringLiteral("disabled")).toBool()) {
        return {false, {}, {}, QStringLiteral("CLI Proxy API account %1 is disabled").arg(resolvedFileName)};
    }
    const QString accessToken = account.value(QStringLiteral("access_token")).toString().trimmed();
    if (accessToken.isEmpty()) {
        return {false, {}, {}, QStringLiteral("No access token in CLI Proxy API account %1").arg(resolvedFileName)};
    }
    // CLI Proxy API owns these tokens and refreshes them in place; Speecher only
    // reads, so an expired token means the proxy has not refreshed it yet.
    const QString expiredValue = account.value(QStringLiteral("expired")).toString();
    const QDateTime expiry = QDateTime::fromString(expiredValue, Qt::ISODate);
    if (!expiredValue.isEmpty() && !expiry.isValid()) {
        return {false, {}, {},
                QStringLiteral("Could not parse expiry \"%1\" in CLI Proxy API account %2").arg(expiredValue, resolvedFileName)};
    }
    if (expiry.isValid() && expiry <= QDateTime::currentDateTimeUtc()) {
        return {false, {}, {},
                QStringLiteral("CLI Proxy API token for %1 is expired; run CLI Proxy API to refresh it").arg(resolvedFileName)};
    }
    return {true, accessToken, account.value(QStringLiteral("account_id")).toString(), {}};
}

} // namespace speecher
