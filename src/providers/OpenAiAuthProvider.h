#pragma once

#include "core/ProviderFailure.h"

#include <QString>

#include <optional>

namespace speecher {

class SecretStore;
struct AccountSettings;
struct RefinementSettings;

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
    // Why there is no usable credential: a missing or expired sign-in unless
    // a refresh said otherwise.
    ProviderFailureKind kind = ProviderFailureKind::Unavailable;
    int httpStatus = 0;

    std::optional<ProviderFailure> failure() const
    {
        return ok ? std::nullopt : std::optional(ProviderFailure{kind, status, httpStatus});
    }
};

// What the Accounts page says about the sign-in, and whether it can be used.
struct OpenAiAuthStatus {
    QString text;
    bool ready = false;
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

    // The sign-in Codex dictation uses: a ChatGPT sign-in, never an API key,
    // so the Codex app's own unless the OpenAI auth mode is cliproxy. Its
    // websocket connects to OpenAI itself, so a CLI Proxy API server plays
    // no part.
    static OpenAiAuthProvider forSpeech(const AccountSettings &accounts);
    // The sign-in OpenAI refinement uses with these settings; secretStore
    // reads the saved API key.
    static OpenAiAuthProvider forRefinement(SecretStore *secretStore,
                                            const AccountSettings &accounts,
                                            const RefinementSettings &refinement);

    OpenAiAuth resolve(bool refreshExpired = true) const;
    OpenAiAuthStatus status() const;
    // Whether the mode's ChatGPT sign-in has expired and a refresh could
    // renew it. Reads the credentials, so worker threads only.
    bool requiresCodexOauthRefresh() const;
    // Whether the mode may use a ChatGPT sign-in, which can expire and renew;
    // decided from the settings alone.
    bool mayUseCodexOauth() const;
    // Whether the ChatGPT sign-in last read for the mode has expired
    // by now, without reading anything: what the GUI thread asks.
    bool codexOauthExpiredAsLastRead() const;
    OpenAiAuth refreshCodexOauth() const;

private:
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
