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
    explicit OpenAiAuthProvider(SecretStore *secretStore = nullptr, const QString &mode = QStringLiteral("auto"));

    OpenAiAuth resolve() const;
    QString status() const;

private:
    static QString readCodexApiKey(QString *status);
    static OpenAiAuth readCodexOauth();
    SecretStore *m_secretStore = nullptr;
    QString m_mode;
};

} // namespace speecher
