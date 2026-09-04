#include "providers/ClaudeCredentials.h"

#include "core/CliToolDiscovery.h"

#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <QTimeZone>
#include <QUrl>

namespace speecher {

namespace {

constexpr auto claudeOauthClientId = "9d1c250a-e61b-44d9-88ed-5944d1962f5e";
constexpr auto claudeOauthTokenUrl = "https://platform.claude.com/v1/oauth/token";
constexpr int refreshTimeoutMs = 30000;

ClaudeCredentialResult readCredentials(const QString &path)
{
    ClaudeCredentialResult result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("Claude credentials not found at %1; run claude in a terminal and use the /login command").arg(path);
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
    result.expiresAt = QDateTime::fromSecsSinceEpoch(expires / (expires > 9999999999LL ? 1000 : 1),
                                                     QTimeZone::UTC);
    for (const QJsonValue &scope : oauth.value(QStringLiteral("scopes")).toArray()) {
        result.scopes << scope.toString();
    }

    if (result.accessToken.isEmpty()) {
        result.error = QStringLiteral("Claude credentials do not contain claudeAiOauth.accessToken");
        return result;
    }
    if (result.expiresAt.isValid() && result.expiresAt <= QDateTime::currentDateTimeUtc()) {
        result.error = QStringLiteral("Claude login expired; run claude in a terminal and use the /login command");
        return result;
    }

    result.ok = true;
    return result;
}

QString findClaudeExecutable()
{
    return CliToolDiscovery::claudeCodeExecutable();
}

QStringList defaultOauthScopes()
{
    return {
        QStringLiteral("user:profile"),
        QStringLiteral("user:inference"),
        QStringLiteral("user:sessions:claude_code"),
        QStringLiteral("user:mcp_servers"),
        QStringLiteral("user:file_upload"),
    };
}

QString tokenUrl()
{
    const QByteArray testUrl = qgetenv("SPEECHER_TEST_CLAUDE_TOKEN_URL");
    if (!testUrl.isEmpty()) {
        return QString::fromUtf8(testUrl);
    }
    return QString::fromLatin1(claudeOauthTokenUrl);
}

bool saveRefreshedCredentials(const QString &path,
                              const QString &sourceRefreshToken,
                              const QString &accessToken,
                              const QString &refreshToken,
                              qint64 expiresAtMs,
                              const QStringList &scopes,
                              QString *error)
{
    QFile source(path);
    if (!source.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("Could not reopen Claude credentials after refresh");
        }
        return false;
    }
    QJsonParseError parseError;
    QJsonDocument document = QJsonDocument::fromJson(source.readAll(), &parseError);
    source.close();
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = QStringLiteral("Claude credentials changed during refresh and are no longer valid JSON");
        }
        return false;
    }

    QJsonObject root = document.object();
    QJsonObject oauth = root.value(QStringLiteral("claudeAiOauth")).toObject();
    if (oauth.value(QStringLiteral("refreshToken")).toString() != sourceRefreshToken) {
        if (error) {
            *error = QStringLiteral("Claude credentials changed during refresh; try again");
        }
        return false;
    }
    oauth.insert(QStringLiteral("accessToken"), accessToken);
    oauth.insert(QStringLiteral("refreshToken"), refreshToken);
    oauth.insert(QStringLiteral("expiresAt"), double(expiresAtMs));
    QJsonArray scopeArray;
    for (const QString &scope : scopes) {
        scopeArray.append(scope);
    }
    oauth.insert(QStringLiteral("scopes"), scopeArray);
    root.insert(QStringLiteral("claudeAiOauth"), oauth);

    QSaveFile destination(path);
    destination.setDirectWriteFallback(false);
    if (!destination.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = QStringLiteral("Could not safely save refreshed Claude credentials");
        }
        return false;
    }
    destination.setPermissions(QFileInfo(path).permissions());
    if (destination.write(QJsonDocument(root).toJson()) < 0
        || !destination.commit()) {
        if (error) {
            *error = QStringLiteral("Could not safely save refreshed Claude credentials");
        }
        return false;
    }
    return true;
}

bool refreshClaudeAuth(const QString &path, const ClaudeCredentialResult &credentials, QString *error)
{
    if (credentials.refreshToken.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Claude login cannot be refreshed; run claude in a terminal and use the /login command");
        }
        return false;
    }

    const QStringList requestedScopes = credentials.scopes.isEmpty()
        ? defaultOauthScopes()
        : credentials.scopes;
    const QJsonObject body{
        {QStringLiteral("grant_type"), QStringLiteral("refresh_token")},
        {QStringLiteral("refresh_token"), credentials.refreshToken},
        {QStringLiteral("client_id"), QString::fromLatin1(claudeOauthClientId)},
        {QStringLiteral("scope"), requestedScopes.join(QLatin1Char(' '))},
    };

    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(tokenUrl())};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *reply = manager.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
    timeout.start(refreshTimeoutMs);
    loop.exec();
    const bool timedOut = !timeout.isActive();
    timeout.stop();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray responseBytes = reply->readAll();
    const QNetworkReply::NetworkError networkError = reply->error();
    reply->deleteLater();
    if (timedOut) {
        if (error) {
            *error = QStringLiteral("Timed out refreshing Claude login; check the network and try again");
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonObject response = QJsonDocument::fromJson(responseBytes, &parseError).object();
    if (status < 200 || status >= 300 || networkError != QNetworkReply::NoError) {
        if (error) {
            const QString code = response.value(QStringLiteral("error")).toString();
            *error = code == QStringLiteral("invalid_grant")
                ? QStringLiteral("Claude login expired; run claude in a terminal and use the /login command")
                : QStringLiteral("Could not refresh Claude login (HTTP %1); check the network and try again").arg(status);
        }
        return false;
    }
    if (parseError.error != QJsonParseError::NoError) {
        if (error) {
            *error = QStringLiteral("Claude login refresh returned invalid JSON");
        }
        return false;
    }

    const QString accessToken = response.value(QStringLiteral("access_token")).toString();
    const QString refreshToken = response.value(QStringLiteral("refresh_token")).toString(credentials.refreshToken);
    const qint64 expiresIn = qRound64(response.value(QStringLiteral("expires_in")).toDouble());
    if (accessToken.isEmpty() || expiresIn <= 0) {
        if (error) {
            *error = QStringLiteral("Claude login refresh response was incomplete");
        }
        return false;
    }

    QStringList refreshedScopes = response.value(QStringLiteral("scope")).toString().split(
        QRegularExpression(QStringLiteral("\\s+")),
        Qt::SkipEmptyParts);
    if (refreshedScopes.isEmpty()) {
        refreshedScopes = requestedScopes;
    }
    return saveRefreshedCredentials(path,
                                    credentials.refreshToken,
                                    accessToken,
                                    refreshToken,
                                    QDateTime::currentMSecsSinceEpoch() + expiresIn * 1000,
                                    refreshedScopes,
                                    error);
}

} // namespace

QString ClaudeCredentials::installedVersion()
{
    const QString executable = findClaudeExecutable();
    if (executable.isEmpty()) {
        return {};
    }

    const QFileInfo executableInfo(executable);
    struct VersionCacheEntry {
        QDateTime modified;
        QString version;
    };
    static QHash<QString, VersionCacheEntry> cache;
    static QMutex cacheMutex;
    const QMutexLocker cacheLock(&cacheMutex);
    const QString cacheKey = executableInfo.absoluteFilePath();
    const QDateTime modified = executableInfo.lastModified();
    const auto cached = cache.constFind(cacheKey);
    if (cached != cache.cend() && cached->modified == modified) {
        return cached->version;
    }

    static const QRegularExpression versionPattern(QStringLiteral("\\b\\d+\\.\\d+\\.\\d+(?:[-+][A-Za-z0-9._-]+)?\\b"));
    const QString version = [&] {
        const QRegularExpressionMatch pathMatch = versionPattern.match(executableInfo.fileName());
        if (pathMatch.hasMatch()) {
            return pathMatch.captured(0);
        }

        QProcess process;
        process.setProgram(executable);
        process.setArguments({QStringLiteral("--version")});
        process.start();
        if (!process.waitForStarted(1000)) {
            return QString();
        }
        if (!process.waitForFinished(2000)) {
            process.kill();
            process.waitForFinished(1000);
            return QString();
        }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            return QString();
        }

        const QString output = QString::fromUtf8(
            process.readAllStandardOutput() + process.readAllStandardError());
        const QRegularExpressionMatch outputMatch = versionPattern.match(output);
        return outputMatch.hasMatch() ? outputMatch.captured(0) : QString();
    }();
    cache.insert(cacheKey, {modified, version});
    return version;
}

ClaudeCredentialResult ClaudeCredentials::load(const QString &path, bool refreshExpired)
{
    ClaudeCredentialResult result = readCredentials(path);
    if (result.ok || !refreshExpired || !result.expiresAt.isValid()
        || result.expiresAt > QDateTime::currentDateTimeUtc()) {
        return result;
    }

    QLockFile lock(path + QStringLiteral(".lock"));
    if (!lock.tryLock(1000)) {
        result.error = QStringLiteral("Could not lock Claude credentials for refresh");
        return result;
    }

    result = readCredentials(path);
    if (result.ok || !result.expiresAt.isValid()
        || result.expiresAt > QDateTime::currentDateTimeUtc()) {
        return result;
    }

    QString refreshError;
    if (!refreshClaudeAuth(path, result, &refreshError)) {
        result.error = refreshError;
        return result;
    }

    ClaudeCredentialResult refreshed = readCredentials(path);
    if (!refreshed.ok) {
        refreshed.error = QStringLiteral("Claude login refresh did not produce valid credentials; %1").arg(refreshed.error);
    }
    return refreshed;
}

bool ClaudeCredentials::requiresRefresh(const QString &path)
{
    const ClaudeCredentialResult result = readCredentials(path);
    return !result.ok && result.expiresAt.isValid()
        && result.expiresAt <= QDateTime::currentDateTimeUtc();
}

} // namespace speecher
