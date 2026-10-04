#include "providers/ClaudeCredentials.h"
#include "providers/ClaudeCredentialStorage.h"
#include "providers/OauthTokenRequest.h"
#include "providers/ProviderFailureClassification.h"

#include "core/CliToolDiscovery.h"

#include <QEventLoop>
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
#include <QTimer>
#include <QTimeZone>
#include <QUrl>

#include <optional>

namespace speecher {

namespace {

constexpr auto claudeOauthClientId = "9d1c250a-e61b-44d9-88ed-5944d1962f5e";
constexpr auto claudeOauthTokenUrl = "https://platform.claude.com/v1/oauth/token";
constexpr int refreshTimeoutMs = 30000;

ClaudeCredentialResult readCredentials(const ClaudeCredentialStorage &storage)
{
    ClaudeCredentialResult result;
    const QByteArray bytes = storage.read(&result.error);
    if (!result.error.isEmpty()) return result;

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        result.error = QStringLiteral("Claude credentials are not valid JSON");
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

QJsonObject withClaudeOauth(QJsonObject root, const QString &accessToken, const QString &refreshToken,
                            qint64 expiresAtMs, const QStringList &scopes)
{
    QJsonObject oauth = root.value(QStringLiteral("claudeAiOauth")).toObject();
    oauth.insert(QStringLiteral("accessToken"), accessToken);
    oauth.insert(QStringLiteral("refreshToken"), refreshToken);
    oauth.insert(QStringLiteral("expiresAt"), double(expiresAtMs));
    oauth.insert(QStringLiteral("scopes"), QJsonArray::fromStringList(scopes));
    root.insert(QStringLiteral("claudeAiOauth"), oauth);
    return root;
}

bool saveRefreshedCredentials(const ClaudeCredentialStorage &storage,
                              const QString &sourceRefreshToken,
                              const QString &accessToken,
                              const QString &refreshToken,
                              qint64 expiresAtMs,
                              const QStringList &scopes,
                              QString *error)
{
    const QByteArray bytes = storage.read(error);
    if (!error->isEmpty()) return false;
    QJsonParseError parseError;
    QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
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
    return storage.write(QJsonDocument(withClaudeOauth(root, accessToken, refreshToken, expiresAtMs, scopes))
                             .toJson(QJsonDocument::Compact), error);
}

// Why the refresh failed, or nothing once it saved new credentials.
std::optional<ProviderFailure> refreshClaudeAuth(const ClaudeCredentialStorage &storage,
                                                 const ClaudeCredentialResult &credentials)
{
    const auto unavailable = [](const QString &message) {
        return ProviderFailure{ProviderFailureKind::Unavailable, message};
    };
    if (credentials.refreshToken.isEmpty()) {
        return unavailable(QStringLiteral("Claude login cannot be refreshed; run claude in a terminal and use the /login command"));
    }

    const QStringList requestedScopes = credentials.scopes.isEmpty()
        ? defaultOauthScopes()
        : credentials.scopes;
    QString error;
    const QByteArray current = storage.read(&error);
    if (!error.isEmpty()) return unavailable(error);
    const QJsonObject preflight = withClaudeOauth(QJsonDocument::fromJson(current).object(),
        credentials.accessToken, credentials.refreshToken, QDateTime::currentMSecsSinceEpoch(), requestedScopes);
    if (!storage.canWrite(QJsonDocument(preflight).toJson(QJsonDocument::Compact), &error)) return unavailable(error);
    const QJsonObject body{
        {QStringLiteral("grant_type"), QStringLiteral("refresh_token")},
        {QStringLiteral("refresh_token"), credentials.refreshToken},
        {QStringLiteral("client_id"), QString::fromLatin1(claudeOauthClientId)},
        {QStringLiteral("scope"), requestedScopes.join(QLatin1Char(' '))},
    };

    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(tokenUrl())};
    applyTokenRequestHeaders(request);
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
        return ProviderFailure{ProviderFailureKind::Timeout,
                               QStringLiteral("Timed out refreshing Claude login; check the network and try again")};
    }

    QJsonParseError parseError;
    const QJsonObject response = QJsonDocument::fromJson(responseBytes, &parseError).object();
    if (status < 200 || status >= 300 || networkError != QNetworkReply::NoError) {
        if (response.value(QStringLiteral("error")).toString() == QStringLiteral("invalid_grant")) {
            return ProviderFailure{ProviderFailureKind::Authentication,
                                   QStringLiteral("Claude login expired; run claude in a terminal and use the /login command"),
                                   status};
        }
        // A refresh that fails for no reason of its own still leaves no
        // usable sign-in.
        ProviderFailure failure = replyFailure(*reply,
            QStringLiteral("Could not refresh Claude login (HTTP %1); check the network and try again").arg(status));
        if (failure.kind == ProviderFailureKind::Other) failure.kind = ProviderFailureKind::Unavailable;
        return failure;
    }
    if (parseError.error != QJsonParseError::NoError) {
        return unavailable(QStringLiteral("Claude login refresh returned invalid JSON"));
    }

    const QString accessToken = response.value(QStringLiteral("access_token")).toString();
    const QString refreshToken = response.value(QStringLiteral("refresh_token")).toString(credentials.refreshToken);
    const qint64 expiresIn = qRound64(response.value(QStringLiteral("expires_in")).toDouble());
    if (accessToken.isEmpty() || expiresIn <= 0) {
        return unavailable(QStringLiteral("Claude login refresh response was incomplete"));
    }

    QStringList refreshedScopes = response.value(QStringLiteral("scope")).toString().split(
        QRegularExpression(QStringLiteral("\\s+")),
        Qt::SkipEmptyParts);
    if (refreshedScopes.isEmpty()) {
        refreshedScopes = requestedScopes;
    }
    if (!saveRefreshedCredentials(storage,
                                  credentials.refreshToken,
                                  accessToken,
                                  refreshToken,
                                  QDateTime::currentMSecsSinceEpoch() + expiresIn * 1000,
                                  refreshedScopes,
                                  &error)) {
        return unavailable(error);
    }
    return std::nullopt;
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
    const ClaudeCredentialStorage storage(path);
    ClaudeCredentialResult result = readCredentials(storage);
    if (result.ok || !refreshExpired || !result.expiresAt.isValid()
        || result.expiresAt > QDateTime::currentDateTimeUtc()) {
        return result;
    }

    QLockFile lock(storage.lockPath());
    if (!lock.tryLock(1000)) {
        result.error = QStringLiteral("Could not lock Claude credentials for refresh");
        return result;
    }

    result = readCredentials(storage);
    if (result.ok || !result.expiresAt.isValid()
        || result.expiresAt > QDateTime::currentDateTimeUtc()) {
        return result;
    }

    if (const std::optional<ProviderFailure> failure = refreshClaudeAuth(storage, result)) {
        result.error = failure->message;
        result.kind = failure->kind;
        result.httpStatus = failure->httpStatus;
        return result;
    }

    ClaudeCredentialResult refreshed = readCredentials(storage);
    if (!refreshed.ok) {
        refreshed.error = QStringLiteral("Claude login refresh did not produce valid credentials; %1").arg(refreshed.error);
    }
    return refreshed;
}

bool ClaudeCredentials::requiresRefresh(const QString &path)
{
    const ClaudeCredentialStorage storage(path);
    const ClaudeCredentialResult result = readCredentials(storage);
    return !result.ok && result.expiresAt.isValid()
        && result.expiresAt <= QDateTime::currentDateTimeUtc();
}

} // namespace speecher
