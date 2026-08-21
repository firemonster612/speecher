#pragma once

#include <QString>

namespace speecher {

class SecretStore;

struct OpenAiAuth {
    bool ok = false;
    QString bearerToken;
    QString source;
    QString status;
    QString organization;
    QString project;
    QString endpointBase;
    QString accountId;
    bool chatgptBackend = false;
};

class OpenAiAuthProvider {
public:
    explicit OpenAiAuthProvider(SecretStore *secretStore = nullptr,
                                const QString &mode = QStringLiteral("auto"),
                                const QString &cliproxyAccount = {},
                                const QString &cliproxyDir = {},
                                const QString &settingsApiKey = {},
                                const QString &settingsStatus = {},
                                const QString &cliproxyBaseUrl = {},
                                const QString &cliproxyApiKey = {});

    OpenAiAuth resolve(bool refreshExpired = true) const;
    QString status() const;
    bool requiresCodexOauthRefresh() const;
    OpenAiAuth refreshCodexOauth() const;

private:
    static QString readCodexApiKey(QString *status);
    static OpenAiAuth readCodexOauth(bool refreshExpired = true);
    SecretStore *m_secretStore = nullptr;
    QString m_mode;
    QString m_cliproxyAccount;
    QString m_cliproxyDir;
    QString m_settingsApiKey;
    QString m_settingsStatus;
    QString m_cliproxyBaseUrl;
    QString m_cliproxyApiKey;
};

} // namespace speecher
