#include "providers/AnthropicAuthProvider.h"

#include "providers/ClaudeCredentials.h"
#include "providers/CliProxyCredentials.h"

namespace speecher {

namespace {

const QString cliproxyMode = QStringLiteral("cliproxy");
const QString cliproxyAccountType = QStringLiteral("claude");

} // namespace

AnthropicAuthProvider::AnthropicAuthProvider(const AccountSettings &accounts,
                                             const QString &serverBase,
                                             const QString &serverKey)
    : m_accounts(accounts)
    , m_serverBase(serverBase)
    , m_serverKey(serverKey)
{
}

AnthropicAuthProvider AnthropicAuthProvider::forSpeech(const AccountSettings &accounts)
{
    return AnthropicAuthProvider(accounts, {}, {});
}

AnthropicAuthProvider AnthropicAuthProvider::forRefinement(const AccountSettings &accounts,
                                                           const RefinementSettings &refinement)
{
    return AnthropicAuthProvider(accounts, refinement.cliproxyBaseUrl, refinement.cliproxyApiKey);
}

AnthropicAuth AnthropicAuthProvider::resolve(bool refreshExpired) const
{
    if (m_accounts.anthropicAuthMode != cliproxyMode) {
        const ClaudeCredentialResult credentials =
            ClaudeCredentials::load(m_accounts.claudeCredentialsPath, refreshExpired);
        if (!credentials.ok) {
            return {false, {}, credentials.error, credentials.kind, credentials.httpStatus};
        }
        return {true, credentials.accessToken};
    }
    // Remote proxy: authenticate with the CLI Proxy API server key; the
    // server picks the account and refreshes its own oauth tokens.
    if (!m_serverBase.isEmpty()) {
        if (m_serverKey.isEmpty()) {
            return {false, {}, QStringLiteral("CLI Proxy API key is not set (cliproxy/apiKey)")};
        }
        return {true, m_serverKey};
    }
    const CliProxyCredentialResult credentials = refreshExpired
        ? CliProxyCredentials::loadWithRefresh(m_accounts.cliproxyOauthDir, cliproxyAccountType,
                                               m_accounts.anthropicCliproxyAccount)
        : CliProxyCredentials::load(m_accounts.cliproxyOauthDir, cliproxyAccountType,
                                    m_accounts.anthropicCliproxyAccount);
    return {credentials.ok, credentials.ok ? credentials.accessToken : QString(), credentials.error, credentials.kind,
            credentials.httpStatus};
}

bool AnthropicAuthProvider::requiresRefresh() const
{
    if (m_accounts.anthropicAuthMode != cliproxyMode) {
        return ClaudeCredentials::requiresRefresh(m_accounts.claudeCredentialsPath);
    }
    if (!m_serverBase.isEmpty()) {
        return false; // the server owns and refreshes its accounts
    }
    return CliProxyCredentials::accountNeedsRefresh(m_accounts.cliproxyOauthDir, cliproxyAccountType,
                                                    m_accounts.anthropicCliproxyAccount);
}

} // namespace speecher
