#include "providers/OpenAiAuthProvider.h"

#include "core/SecretStore.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>

namespace speecher {

OpenAiAuthProvider::OpenAiAuthProvider(SecretStore *secretStore, const QString &mode)
    : m_secretStore(secretStore)
    , m_mode(mode)
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

ApiKeyCandidate readCodexApiKeyCandidate(QString *status)
{
    QFile file(QDir::homePath() + QStringLiteral("/.codex/auth.json"));
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
    QFile file(QDir::homePath() + QStringLiteral("/.codex/auth.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("auth_mode")).toString();
}

OpenAiAuth OpenAiAuthProvider::readCodexOauth()
{
    QFile file(QDir::homePath() + QStringLiteral("/.codex/auth.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {false, {}, QStringLiteral("codex_oauth"), QStringLiteral("No Codex auth file"), {}, {}, {}, {}, true};
    }
    const QJsonObject tokens = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("tokens")).toObject();
    const QString accessToken = tokens.value(QStringLiteral("access_token")).toString().trimmed();
    if (accessToken.isEmpty()) {
        return {false, {}, QStringLiteral("codex_oauth"), QStringLiteral("No Codex OAuth token found"), {}, {}, {}, {}, true};
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

OpenAiAuth OpenAiAuthProvider::resolve() const
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
        return readCodexOauth();
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
    if (mode == QStringLiteral("settings")) {
        if (m_secretStore && m_secretStore->apiKey().startsWith(QStringLiteral("sk-"))) {
            return {true,
                    m_secretStore->apiKey(),
                    QStringLiteral("settings"),
                    m_secretStore->status(),
                    {},
                    {},
                    QStringLiteral("https://api.openai.com/v1"),
                    {},
                    false};
        }
        return {false, {}, QStringLiteral("settings"), QStringLiteral("Settings API key not found"), {}, {}, {}, {}, false};
    }

    if (codexAuthMode() == QStringLiteral("chatgpt")) {
        OpenAiAuth oauth = readCodexOauth();
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
    OpenAiAuth oauth = readCodexOauth();
    if (oauth.ok) {
        return oauth;
    }
    const ApiKeyCandidate envKey = readEnvApiKey();
    if (envKey.key.startsWith(QStringLiteral("sk-"))) {
        OpenAiAuth auth = authFromCandidate(envKey, QStringLiteral("env"), QStringLiteral("OPENAI_API_KEY found"));
        auth.endpointBase = QStringLiteral("https://api.openai.com/v1");
        return auth;
    }
    if (m_secretStore && m_secretStore->apiKey().startsWith(QStringLiteral("sk-"))) {
        return {true,
                m_secretStore->apiKey(),
                QStringLiteral("settings"),
                m_secretStore->status(),
                {},
                {},
                QStringLiteral("https://api.openai.com/v1"),
                {},
                false};
    }
    return {false, {}, {}, QStringLiteral("No OpenAI credential found"), {}, {}, {}, {}, false};
}

QString OpenAiAuthProvider::status() const
{
    return resolve().status;
}

} // namespace speecher
