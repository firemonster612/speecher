#pragma once

#include "core/AppSettings.h"
#include "core/ProviderFailure.h"

#include <QString>

#include <optional>

// The Anthropic provider family's sign-in, which Claude Voice dictation and
// Anthropic refinement share: Claude Code's own credentials or, in the
// cliproxy auth mode, a CLI Proxy API Account file or, for refinement only,
// the CLI Proxy API server's key.

namespace speecher {

struct AnthropicAuth {
    bool ok = false;
    QString accessToken;
    // Why there is no token.
    QString error;
    // A missing or expired sign-in unless a refresh said otherwise.
    ProviderFailureKind kind = ProviderFailureKind::Unavailable;
    int httpStatus = 0;

    std::optional<ProviderFailure> failure() const
    {
        return ok ? std::nullopt : std::optional(ProviderFailure{kind, error, httpStatus});
    }
};

class AnthropicAuthProvider {
public:
    // Claude Voice dictation's sign-in. Its websocket connects to Anthropic
    // itself, so it reads the account files even while refinement goes
    // through a CLI Proxy API server.
    static AnthropicAuthProvider forSpeech(const AccountSettings &accounts);
    // Anthropic refinement's, through refinement's CLI Proxy API server when
    // one is set.
    static AnthropicAuthProvider forRefinement(const AccountSettings &accounts, const RefinementSettings &refinement);

    // The access token the sign-in points at. With refreshExpired an expired
    // token is renewed over the network first, so only a worker thread may
    // ask for that.
    AnthropicAuth resolve(bool refreshExpired) const;
    // Whether that token has expired and a refresh could renew it.
    bool requiresRefresh() const;

private:
    AnthropicAuthProvider(const AccountSettings &accounts, const QString &serverBase, const QString &serverKey);

    AccountSettings m_accounts;
    QString m_serverBase;
    QString m_serverKey;
};

} // namespace speecher
