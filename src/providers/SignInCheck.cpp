#include "providers/SignInCheck.h"

#include "core/AppSettings.h"
#include "providers/AnthropicTranscriptRefiner.h"
#include "providers/CliProxyCredentials.h"
#include "providers/OpenAiAuthProvider.h"

namespace speecher {

namespace {

const QString cliproxyAccountTypes[] = {QStringLiteral("codex"), QStringLiteral("claude")};

OpenAiAuthProvider openAiAuth(const RefinementSettings &settings, SecretStore *secrets)
{
    return OpenAiAuthProvider(secrets,
                              settings.openAiAuthMode,
                              settings.openAiCliproxyAccount,
                              settings.cliproxyOauthDir,
                              {},
                              {},
                              settings.cliproxyBaseUrl,
                              settings.cliproxyApiKey);
}

SignInCheck checkOpenAi(const RefinementSettings &settings, SecretStore *secrets)
{
    const OpenAiAuthProvider provider = openAiAuth(settings, secrets);
    const OpenAiAuthStatus status = provider.status();
    if (status.ready) {
        return {SignInState::SignedIn, status.text};
    }
    return {provider.requiresCodexOauthRefresh() ? SignInState::Expired : SignInState::Missing, status.text};
}

SignInCheck checkAnthropic(const RefinementSettings &settings)
{
    const RefinementPrepareResult token = loadAnthropicAccessToken(settings, nullptr, false);
    if (token.ok) {
        return {SignInState::SignedIn, token.message};
    }
    return {anthropicSignInNeedsRefresh(settings) ? SignInState::Expired : SignInState::Missing, token.message};
}

// Signed in while the server has its key, or any enabled account file is
// current; expired when every enabled one has run out.
SignInCheck checkCliproxy(const RefinementSettings &settings)
{
    if (!settings.cliproxyBaseUrl.isEmpty()) {
        if (settings.cliproxyApiKey.isEmpty()) {
            return {SignInState::Missing, QStringLiteral("CLI Proxy API key is not set (cliproxy/apiKey)")};
        }
        return {SignInState::SignedIn, QStringLiteral("Using CLI Proxy API at %1").arg(settings.cliproxyBaseUrl)};
    }
    const QString &directory = settings.cliproxyOauthDir;
    bool expired = false;
    for (const QString &type : cliproxyAccountTypes) {
        for (const CliProxyAccount &account : CliProxyCredentials::listAccounts(directory, type)) {
            if (account.disabled) {
                continue;
            }
            if (!account.expired) {
                return {SignInState::SignedIn, QStringLiteral("CLI Proxy API accounts found in %1").arg(directory)};
            }
            expired = true;
        }
    }
    if (expired) {
        return {SignInState::Expired, QStringLiteral("Every CLI Proxy API account in %1 is expired").arg(directory)};
    }
    return {SignInState::Missing, QStringLiteral("No CLI Proxy API accounts found in %1").arg(directory)};
}

// Why renewing failed, or nothing.
QString renewCliproxyAccounts(const RefinementSettings &settings)
{
    if (!settings.cliproxyBaseUrl.isEmpty()) {
        return {}; // the server owns and refreshes its accounts
    }
    QString failure;
    for (const QString &type : cliproxyAccountTypes) {
        for (const CliProxyAccount &account : CliProxyCredentials::listAccounts(settings.cliproxyOauthDir, type)) {
            if (!account.expired || account.disabled) {
                continue;
            }
            const CliProxyCredentialResult renewed =
                CliProxyCredentials::loadWithRefresh(settings.cliproxyOauthDir, type, account.fileName);
            if (!renewed.ok && failure.isEmpty()) {
                failure = renewed.error;
            }
        }
    }
    return failure;
}

// Why renewing the sign-in failed, or nothing once it is current.
QString renew(SignInService service, const RefinementSettings &settings, SecretStore *secrets)
{
    switch (service) {
    case SignInService::OpenAi: {
        const OpenAiAuth renewed = openAiAuth(settings, secrets).resolve(true);
        return renewed.ok ? QString() : renewed.status;
    }
    case SignInService::Anthropic: {
        const RefinementPrepareResult renewed = loadAnthropicAccessToken(settings, nullptr, true);
        return renewed.ok ? QString() : renewed.message;
    }
    case SignInService::CliProxy:
        return renewCliproxyAccounts(settings);
    }
    return {};
}

} // namespace

SignInCheck checkSignIn(SignInService service, const RefinementSettings &settings, SecretStore *secrets)
{
    switch (service) {
    case SignInService::OpenAi:
        return checkOpenAi(settings, secrets);
    case SignInService::Anthropic:
        return checkAnthropic(settings);
    case SignInService::CliProxy:
        return checkCliproxy(settings);
    }
    return {};
}

SignInCheck renewSignIn(SignInService service, const RefinementSettings &settings, SecretStore *secrets)
{
    const QString failure = renew(service, settings, secrets);
    SignInCheck check = checkSignIn(service, settings, secrets);
    // The refresh's own error says more than "expired".
    if (check.state != SignInState::SignedIn && !failure.isEmpty()) {
        check.detail = failure;
    }
    return check;
}

} // namespace speecher
