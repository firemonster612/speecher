#include "providers/OpenAiAuthProvider.h"

#include "core/CliToolDiscovery.h"
#include "core/SecretStore.h"
#include "providers/CliProxyCredentials.h"

#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimeZone>

namespace speecher {

OpenAiAuthProvider::OpenAiAuthProvider(SecretStore *secretStore,
                                       const QString &mode,
                                       const QString &cliproxyAccount,
                                       const QString &cliproxyDir,
                                       const QString &settingsApiKey,
                                       const QString &settingsStatus,
                                       const QString &cliproxyBaseUrl,
                                       const QString &cliproxyApiKey)
    : m_secretStore(secretStore)
    , m_mode(mode)
    , m_cliproxyAccount(cliproxyAccount)
    , m_cliproxyDir(cliproxyDir)
    , m_settingsApiKey(settingsApiKey)
    , m_settingsStatus(settingsStatus)
    , m_cliproxyBaseUrl(cliproxyBaseUrl)
    , m_cliproxyApiKey(cliproxyApiKey)
{
}

struct ApiKeyCandidate {
    QString key;
    QString organization;
    QString project;
};

static QString firstStringValue(const QJsonObject &object, const QStringList &keys)
{
    for (const QString &key : keys) {
        const QString value = object.value(key).toString().trimmed();
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

static ApiKeyCandidate objectApiKeyCandidate(const QJsonObject &object)
{
    ApiKeyCandidate candidate;
    for (const QString &key : {QStringLiteral("api_key"), QStringLiteral("key"), QStringLiteral("value")}) {
        const QString value = object.value(key).toString().trimmed();
        if (value.startsWith(QStringLiteral("sk-"))) {
            candidate.key = value;
            break;
        }
    }
    candidate.organization = firstStringValue(object, {QStringLiteral("organization"), QStringLiteral("organization_id"), QStringLiteral("org_id")});
    candidate.project = firstStringValue(object, {QStringLiteral("project"), QStringLiteral("project_id")});
    return candidate;
}

static QString envValue(const QStringList &names)
{
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const QString &name : names) {
        const QString value = env.value(name).trimmed();
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

static QString codexAuthPath()
{
    return QDir::homePath() + QStringLiteral("/.codex/auth.json");
}

static qint64 jwtExpirySecs(const QString &jwt)
{
    const QStringList parts = jwt.split(QLatin1Char('.'));
    if (parts.size() < 2) {
        return 0;
    }
    QJsonParseError parseError;
    const QJsonDocument payload = QJsonDocument::fromJson(QByteArray::fromBase64(parts.at(1).toUtf8(), QByteArray::Base64UrlEncoding),
                                                          &parseError);
    if (parseError.error != QJsonParseError::NoError || !payload.isObject()) {
        return 0;
    }
    return static_cast<qint64>(payload.object().value(QStringLiteral("exp")).toDouble());
}

static bool jwtExpired(const QString &jwt)
{
    const qint64 expires = jwtExpirySecs(jwt);
    return expires > 0 && QDateTime::fromSecsSinceEpoch(expires, QTimeZone::UTC) <= QDateTime::currentDateTimeUtc();
}

static int codexRefreshTimeoutMs()
{
    bool ok = false;
    const int timeoutMs = qEnvironmentVariableIntValue("SPEECHER_CODEX_REFRESH_TIMEOUT_MS", &ok);
    return ok && timeoutMs > 0 ? timeoutMs : 15000;
}

static bool refreshCodexAuth(QString *error)
{
    const QString executable = CliToolDiscovery::codexExecutable();
    if (executable.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Could not find Codex CLI; install it and ensure `codex` is on PATH");
        }
        return false;
    }

    QProcess process;
    process.setProgram(executable);
    process.setArguments({QStringLiteral("exec"), QStringLiteral("i"), QStringLiteral("--skip-git-repo-check")});
    process.start();
    if (!process.waitForStarted(2000)) {
        if (error) {
            *error = QStringLiteral("Could not start Codex OAuth refresh");
        }
        return false;
    }
    process.closeWriteChannel();
    if (!process.waitForFinished(codexRefreshTimeoutMs())) {
        process.kill();
        process.waitForFinished(1000);
        if (error) {
            *error = QStringLiteral("Timed out refreshing Codex OAuth token with `codex exec \"i\" --skip-git-repo-check`");
        }
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) {
            const QString output = QString::fromUtf8(process.readAllStandardError() + process.readAllStandardOutput()).left(240).simplified();
            *error = output.isEmpty()
                ? QStringLiteral("Codex OAuth refresh exited unsuccessfully")
                : QStringLiteral("Codex OAuth refresh exited unsuccessfully: %1").arg(output);
        }
        return false;
    }
    return true;
}

ApiKeyCandidate readCodexApiKeyCandidate(QString *status)
{
    QFile file(codexAuthPath());
    if (!file.open(QIODevice::ReadOnly)) {
        if (status) {
            *status = QStringLiteral("No Codex auth file");
        }
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    const QJsonObject root = doc.object();
    const QJsonValue apiKey = root.value(QStringLiteral("OPENAI_API_KEY"));
    ApiKeyCandidate candidate;
    if (apiKey.isString()) {
        candidate.key = apiKey.toString().trimmed();
    } else if (apiKey.isObject()) {
        candidate = objectApiKeyCandidate(apiKey.toObject());
    }
    if (candidate.organization.isEmpty()) {
        candidate.organization = firstStringValue(root, {QStringLiteral("OPENAI_ORG_ID"), QStringLiteral("OPENAI_ORGANIZATION")});
    }
    if (candidate.project.isEmpty()) {
        candidate.project = firstStringValue(root, {QStringLiteral("OPENAI_PROJECT_ID"), QStringLiteral("OPENAI_PROJECT")});
    }
    if (candidate.key.startsWith(QStringLiteral("sk-"))) {
        if (status) {
            *status = QStringLiteral("Codex API key found");
        }
        return candidate;
    }
    if (status) {
        *status = QStringLiteral("No Codex API key found");
    }
    return {};
}

QString OpenAiAuthProvider::readCodexApiKey(QString *status)
{
    return readCodexApiKeyCandidate(status).key;
}

static OpenAiAuth authFromCandidate(const ApiKeyCandidate &candidate, const QString &source, const QString &status)
{
    return {true,
            candidate.key,
            source,
            status,
            candidate.organization,
            candidate.project,
            QStringLiteral("https://api.openai.com/v1"),
            {},
            false};
}

static ApiKeyCandidate readEnvApiKey()
{
    ApiKeyCandidate candidate;
    candidate.key = envValue({QStringLiteral("OPENAI_API_KEY")});
    candidate.organization = envValue({QStringLiteral("OPENAI_ORG_ID"), QStringLiteral("OPENAI_ORGANIZATION")});
    candidate.project = envValue({QStringLiteral("OPENAI_PROJECT_ID"), QStringLiteral("OPENAI_PROJECT")});
    return candidate;
}

static QString codexAuthMode()
{
    QFile file(codexAuthPath());
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("auth_mode")).toString();
}

OpenAiAuth OpenAiAuthProvider::readCodexOauth(bool refreshExpired)
{
    QFile file(codexAuthPath());
    if (!file.open(QIODevice::ReadOnly)) {
        return {false, {}, QStringLiteral("codex_oauth"), QStringLiteral("No Codex auth file"), {}, {}, {}, {}, true};
    }
    const QJsonObject tokens = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("tokens")).toObject();
    const QString accessToken = tokens.value(QStringLiteral("access_token")).toString().trimmed();
    if (accessToken.isEmpty()) {
        return {false, {}, QStringLiteral("codex_oauth"), QStringLiteral("No Codex OAuth token found"), {}, {}, {}, {}, true};
    }
    if (jwtExpired(accessToken)) {
        if (!refreshExpired) {
            return {false, {}, QStringLiteral("codex_oauth"), QStringLiteral("Codex OAuth token expired"), {}, {}, {}, {}, true};
        }

        QString refreshError;
        if (!refreshCodexAuth(&refreshError)) {
            return {false, {}, QStringLiteral("codex_oauth"), refreshError, {}, {}, {}, {}, true};
        }
        return readCodexOauth(false);
    }
    const QString accountId = tokens.value(QStringLiteral("account_id")).toString().trimmed();
    return {true,
            accessToken,
            QStringLiteral("codex_oauth"),
            QStringLiteral("Codex OAuth token found"),
            {},
            {},
            QStringLiteral("https://chatgpt.com/backend-api/codex"),
            accountId,
            true};
}

OpenAiAuth OpenAiAuthProvider::resolve(bool refreshExpired) const
{
    QString status;
    const QString mode = m_mode.isEmpty() ? QStringLiteral("auto") : m_mode;
    if (mode == QStringLiteral("codex_api_key")) {
        const ApiKeyCandidate codexKey = readCodexApiKeyCandidate(&status);
        if (!codexKey.key.isEmpty()) {
            return authFromCandidate(codexKey, QStringLiteral("codex_api_key"), status);
        }
        return {false, {}, QStringLiteral("codex_api_key"), status, {}, {}, {}, {}, false};
    }
    if (mode == QStringLiteral("codex_oauth")) {
        return readCodexOauth(refreshExpired);
    }
    if (mode == QStringLiteral("env")) {
        const ApiKeyCandidate envKey = readEnvApiKey();
        if (envKey.key.startsWith(QStringLiteral("sk-"))) {
            OpenAiAuth auth = authFromCandidate(envKey, QStringLiteral("env"), QStringLiteral("OPENAI_API_KEY found"));
            auth.endpointBase = QStringLiteral("https://api.openai.com/v1");
            return auth;
        }
        return {false, {}, QStringLiteral("env"), QStringLiteral("OPENAI_API_KEY not found"), {}, {}, {}, {}, false};
    }
    if (mode == QStringLiteral("cliproxy")) {
        // Remote proxy: authenticate with the CLI Proxy API server key; the
        // server picks the account and refreshes its own oauth tokens.
        if (!m_cliproxyBaseUrl.isEmpty()) {
            if (m_cliproxyApiKey.isEmpty()) {
                return {false, {}, QStringLiteral("cliproxy"),
                        QStringLiteral("CLI Proxy API key is not set (cliproxy/apiKey)"), {}, {}, {}, {}, false};
            }
            return {true,
                    m_cliproxyApiKey,
                    QStringLiteral("cliproxy"),
                    QStringLiteral("Using CLI Proxy API at %1").arg(m_cliproxyBaseUrl),
                    {},
                    {},
                    m_cliproxyBaseUrl + QStringLiteral("/v1"),
                    {},
                    false};
        }
        const CliProxyCredentialResult credentials =
            CliProxyCredentials::load(m_cliproxyDir, QStringLiteral("codex"), m_cliproxyAccount);
        if (!credentials.ok) {
            return {false, {}, QStringLiteral("cliproxy"), credentials.error, {}, {}, {}, {}, true};
        }
        return {true,
                credentials.accessToken,
                QStringLiteral("cliproxy"),
                QStringLiteral("CLI Proxy API Codex token found"),
                {},
                {},
                QStringLiteral("https://chatgpt.com/backend-api/codex"),
                credentials.accountId,
                true};
    }
    if (mode == QStringLiteral("settings")) {
        const QString apiKey = m_secretStore ? m_secretStore->apiKey() : m_settingsApiKey;
        if (apiKey.startsWith(QStringLiteral("sk-"))) {
            return {true,
                    apiKey,
                    QStringLiteral("settings"),
                    m_secretStore ? m_secretStore->status() : m_settingsStatus,
                    {},
                    {},
                    QStringLiteral("https://api.openai.com/v1"),
                    {},
                    false};
        }
        return {false, {}, QStringLiteral("settings"), QStringLiteral("Settings API key not found"), {}, {}, {}, {}, false};
    }

    const bool codexUsesChatGpt = codexAuthMode() == QStringLiteral("chatgpt");
    if (codexUsesChatGpt) {
        OpenAiAuth oauth = readCodexOauth(refreshExpired);
        if (oauth.ok) {
            return oauth;
        }
    }

    const ApiKeyCandidate codexKey = readCodexApiKeyCandidate(&status);
    if (!codexKey.key.isEmpty()) {
        OpenAiAuth auth = authFromCandidate(codexKey, QStringLiteral("codex_api_key"), status);
        auth.endpointBase = QStringLiteral("https://api.openai.com/v1");
        return auth;
    }
    if (!codexUsesChatGpt) {
        OpenAiAuth oauth = readCodexOauth(refreshExpired);
        if (oauth.ok) {
            return oauth;
        }
    }
    const ApiKeyCandidate envKey = readEnvApiKey();
    if (envKey.key.startsWith(QStringLiteral("sk-"))) {
        OpenAiAuth auth = authFromCandidate(envKey, QStringLiteral("env"), QStringLiteral("OPENAI_API_KEY found"));
        auth.endpointBase = QStringLiteral("https://api.openai.com/v1");
        return auth;
    }
    const QString settingsApiKey = m_secretStore ? m_secretStore->apiKey() : m_settingsApiKey;
    if (settingsApiKey.startsWith(QStringLiteral("sk-"))) {
        return {true,
                settingsApiKey,
                QStringLiteral("settings"),
                m_secretStore ? m_secretStore->status() : m_settingsStatus,
                {},
                {},
                QStringLiteral("https://api.openai.com/v1"),
                {},
                false};
    }
    return {false, {}, {}, QStringLiteral("No OpenAI credential found"), {}, {}, {}, {}, false};
}

bool OpenAiAuthProvider::requiresCodexOauthRefresh() const
{
    QString status;
    const QString mode = m_mode.isEmpty() ? QStringLiteral("auto") : m_mode;
    if (mode == QStringLiteral("codex_oauth")) {
        return readCodexOauth(false).status == QStringLiteral("Codex OAuth token expired");
    }
    if (mode == QStringLiteral("cliproxy")) {
        if (!m_cliproxyBaseUrl.isEmpty()) {
            return false; // the server owns and refreshes its accounts
        }
        return CliProxyCredentials::accountNeedsRefresh(m_cliproxyDir, QStringLiteral("codex"), m_cliproxyAccount);
    }
    if (mode == QStringLiteral("codex_api_key") || mode == QStringLiteral("env") || mode == QStringLiteral("settings")) {
        return false;
    }

    if (codexAuthMode() == QStringLiteral("chatgpt")) {
        return readCodexOauth(false).status == QStringLiteral("Codex OAuth token expired");
    }

    if (!readCodexApiKeyCandidate(&status).key.isEmpty()) {
        return false;
    }
    return readCodexOauth(false).status == QStringLiteral("Codex OAuth token expired");
}

OpenAiAuth OpenAiAuthProvider::refreshCodexOauth() const
{
    Q_UNUSED(m_secretStore)
    if ((m_mode.isEmpty() ? QStringLiteral("auto") : m_mode) == QStringLiteral("cliproxy")
        && m_cliproxyBaseUrl.isEmpty()) {
        const CliProxyCredentialResult credentials =
            CliProxyCredentials::loadWithRefresh(m_cliproxyDir, QStringLiteral("codex"), m_cliproxyAccount);
        if (!credentials.ok) {
            return {false, {}, QStringLiteral("cliproxy"), credentials.error, {}, {}, {}, {}, true};
        }
        return {true,
                credentials.accessToken,
                QStringLiteral("cliproxy"),
                QStringLiteral("CLI Proxy API Codex token refreshed"),
                {},
                {},
                QStringLiteral("https://chatgpt.com/backend-api/codex"),
                credentials.accountId,
                true};
    }
    return readCodexOauth(true);
}

QString OpenAiAuthProvider::status() const
{
    const OpenAiAuth auth = resolve(false);
    if (!auth.status.isEmpty()) {
        return auth.status;
    }
    return auth.ok ? QStringLiteral("Credentials found") : QStringLiteral("No credentials found");
}

} // namespace speecher
