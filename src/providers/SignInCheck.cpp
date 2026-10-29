#include "providers/SignInCheck.h"

#include "core/AppSettings.h"
#include "core/ProviderTable.h"
#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#include "providers/AnthropicTranscriptRefiner.h"
#include "providers/CliProxyCredentials.h"
#include "providers/OpenAiAuthProvider.h"

namespace speecher {

namespace {

const QString cliproxyMode = QStringLiteral("cliproxy");

// CLI Proxy API's account types, each with the setting that chooses one.
const struct CliproxyAccountType {
    QString type;
    QString RefinementSettings::*chosen;
} cliproxyAccountTypes[] = {
    {QStringLiteral("codex"), &RefinementSettings::openAiCliproxyAccount},
    {QStringLiteral("claude"), &RefinementSettings::anthropicCliproxyAccount},
};

// The CLI Proxy API server's key, when a server is set and the snapshot left
// the key unread. Why the keyring could not answer, or nothing.
QString readServerKey(AppSettings &settings, SecretStore *secrets)
{
    using Secret = SecretStore::Secret;
    if (settings.refinement.cliproxyBaseUrl.isEmpty() || !secrets
        || !settings.unreadSecretKeys.contains(SecretStore::settingsKey(Secret::CliproxyApiKey))) {
        return {};
    }
    settings.refinement.cliproxyApiKey = secrets->secret(Secret::CliproxyApiKey);
    return settings.refinement.cliproxyApiKey.isEmpty() ? secrets->readFailure(Secret::CliproxyApiKey) : QString();
}

// Reads the secret the service's sign-in needs that the snapshot left
// unread. Why the keyring could not answer, or nothing.
QString readSecrets(SignInService service, AppSettings &settings, SecretStore *secrets)
{
    switch (service) {
    case SignInService::OpenAi:
        return SettingsStore::resolveProviderSecrets(settings, ProviderRole::Refinement,
                                                     providerKey(ProviderId::OpenAi));
    case SignInService::Anthropic:
        return SettingsStore::resolveProviderSecrets(settings, ProviderRole::Refinement,
                                                     providerKey(ProviderId::Anthropic));
    case SignInService::CliProxy:
        return readServerKey(settings, secrets);
    }
    return {};
}

SignInCheck checkOpenAi(const RefinementSettings &settings, SecretStore *secrets)
{
    const OpenAiAuthProvider provider = OpenAiAuthProvider::forRefinement(secrets, settings);
    const OpenAiAuthStatus status = provider.status();
    if (status.ready) {
        return {SignInState::SignedIn, status.text};
    }
    // The saved API key is read as the mode needs it, so only now is it known
    // whether the keyring answered.
    if (const QString failure = secrets ? secrets->readFailure(SecretStore::Secret::OpenAiApiKey) : QString();
        !failure.isEmpty()) {
        return {SignInState::Unknown, failure};
    }
    return {provider.requiresCodexOauthRefresh() ? SignInState::Expired : SignInState::Missing, status.text};
}

SignInCheck checkAnthropic(const RefinementSettings &settings)
{
    const bool proxy = settings.anthropicAuthMode == cliproxyMode;
    const QString server = proxy ? settings.cliproxyBaseUrl : QString();
    const QString directory = proxy && server.isEmpty() ? settings.cliproxyOauthDir : QString();
    const RefinementPrepareResult token = loadAnthropicAccessToken(settings, nullptr, false);
    if (token.ok) {
        return {SignInState::SignedIn, {}, server, directory};
    }
    const SignInState state = anthropicSignInNeedsRefresh(settings) ? SignInState::Expired : SignInState::Missing;
    return {state, token.message, server, directory};
}

// Signed in while the server has its key, or any enabled account file loads;
// expired when the enabled ones that could load have all run out. An account
// file load() refuses says why when nothing else is found.
SignInCheck checkCliproxy(const RefinementSettings &settings)
{
    const QString &server = settings.cliproxyBaseUrl;
    if (!server.isEmpty()) {
        if (settings.cliproxyApiKey.isEmpty()) {
            return {SignInState::Missing, QStringLiteral("CLI Proxy API key is not set (cliproxy/apiKey)"), server};
        }
        return {SignInState::SignedIn, {}, server};
    }
    const QString &directory = settings.cliproxyOauthDir;
    bool expired = false;
    QString problem;
    for (const CliproxyAccountType &type : cliproxyAccountTypes) {
        for (const CliProxyAccount &account : CliProxyCredentials::listAccounts(directory, type.type)) {
            if (account.disabled) {
                continue;
            }
            if (!account.problem.isEmpty()) {
                problem = problem.isEmpty() ? account.problem : problem;
                continue;
            }
            if (!account.expired) {
                return {SignInState::SignedIn, {}, {}, directory};
            }
            expired = true;
        }
    }
    if (expired) {
        return {SignInState::Expired, {}, {}, directory};
    }
    return {SignInState::Missing, problem, {}, directory};
}

SignInCheck check(SignInService service, const RefinementSettings &settings, SecretStore *secrets)
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

// Renews the expired account OpenAI and Anthropic each use, leaving the
// others alone: their tokens rotate, and nobody chose them. Why renewing
// failed, or nothing.
QString renewCliproxyAccounts(const RefinementSettings &settings)
{
    if (!settings.cliproxyBaseUrl.isEmpty()) {
        return {}; // the server owns and refreshes its accounts
    }
    QString failure;
    for (const CliproxyAccountType &type : cliproxyAccountTypes) {
        const QString &chosen = settings.*type.chosen;
        if (!CliProxyCredentials::accountNeedsRefresh(settings.cliproxyOauthDir, type.type, chosen)) {
            continue;
        }
        const CliProxyCredentialResult renewed =
            CliProxyCredentials::loadWithRefresh(settings.cliproxyOauthDir, type.type, chosen);
        if (!renewed.ok && failure.isEmpty()) {
            failure = renewed.error;
        }
    }
    return failure;
}

// Why renewing the sign-in failed, or nothing once it is current.
QString renew(SignInService service, const RefinementSettings &settings, SecretStore *secrets)
{
    switch (service) {
    case SignInService::OpenAi: {
        const OpenAiAuth renewed = OpenAiAuthProvider::forRefinement(secrets, settings).resolve(true);
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

SignInCheck checkSignIn(SignInService service, AppSettings settings, SecretStore *secrets)
{
    if (const QString failure = readSecrets(service, settings, secrets); !failure.isEmpty()) {
        return {SignInState::Unknown, failure};
    }
    return check(service, settings.refinement, secrets);
}

SignInCheck renewSignIn(SignInService service, AppSettings settings, SecretStore *secrets)
{
    if (const QString failure = readSecrets(service, settings, secrets); !failure.isEmpty()) {
        return {SignInState::Unknown, failure};
    }
    const QString failure = renew(service, settings.refinement, secrets);
    SignInCheck renewed = check(service, settings.refinement, secrets);
    // The refresh's own error says more than "expired".
    if (renewed.state != SignInState::SignedIn && !failure.isEmpty()) {
        renewed.detail = failure;
    }
    return renewed;
}

} // namespace speecher
