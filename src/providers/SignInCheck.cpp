#include "providers/SignInCheck.h"

#include "core/AppSettings.h"
#include "core/ProviderTable.h"
#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#include "providers/AnthropicAuthProvider.h"
#include "providers/CliProxyCredentials.h"
#include "providers/OpenAiAuthProvider.h"

namespace speecher {

namespace {

const QString cliproxyMode = QStringLiteral("cliproxy");

// CLI Proxy API's account types, each with the setting that chooses one.
const struct CliproxyAccountType {
    QString type;
    QString AccountSettings::*chosen;
} cliproxyAccountTypes[] = {
    {QStringLiteral("codex"), &AccountSettings::openAiCliproxyAccount},
    {QStringLiteral("claude"), &AccountSettings::anthropicCliproxyAccount},
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

SignInCheck checkOpenAi(const AppSettings &settings, SecretStore *secrets)
{
    const OpenAiAuthProvider provider = OpenAiAuthProvider::forRefinement(secrets, settings.accounts, settings.refinement);
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

SignInCheck checkAnthropic(const AppSettings &settings)
{
    const bool proxy = settings.accounts.anthropicAuthMode == cliproxyMode;
    const QString server = proxy ? settings.refinement.cliproxyBaseUrl : QString();
    const QString directory = proxy && server.isEmpty() ? settings.accounts.cliproxyOauthDir : QString();
    const AnthropicAuthProvider signIn = AnthropicAuthProvider::forRefinement(settings.accounts, settings.refinement);
    const AnthropicAuth token = signIn.resolve(false);
    if (token.ok) {
        return {SignInState::SignedIn, {}, server, directory};
    }
    const SignInState state = signIn.requiresRefresh() ? SignInState::Expired : SignInState::Missing;
    return {state, token.error, server, directory};
}

// Signed in while the server has its key, or any enabled account file loads;
// expired when the enabled ones that could load have all run out. An account
// file load() refuses says why when nothing else is found.
SignInCheck checkCliproxy(const AppSettings &settings)
{
    const QString &server = settings.refinement.cliproxyBaseUrl;
    if (!server.isEmpty()) {
        if (settings.refinement.cliproxyApiKey.isEmpty()) {
            return {SignInState::Missing, QStringLiteral("CLI Proxy API key is not set (cliproxy/apiKey)"), server};
        }
        return {SignInState::SignedIn, {}, server};
    }
    const QString &directory = settings.accounts.cliproxyOauthDir;
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

SignInCheck check(SignInService service, const AppSettings &settings, SecretStore *secrets)
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
QString renewCliproxyAccounts(const AppSettings &settings)
{
    if (!settings.refinement.cliproxyBaseUrl.isEmpty()) {
        return {}; // the server owns and refreshes its accounts
    }
    const AccountSettings &accounts = settings.accounts;
    QString failure;
    for (const CliproxyAccountType &type : cliproxyAccountTypes) {
        const QString &chosen = accounts.*type.chosen;
        if (!CliProxyCredentials::accountNeedsRefresh(accounts.cliproxyOauthDir, type.type, chosen)) {
            continue;
        }
        const CliProxyCredentialResult renewed =
            CliProxyCredentials::loadWithRefresh(accounts.cliproxyOauthDir, type.type, chosen);
        if (!renewed.ok && failure.isEmpty()) {
            failure = renewed.error;
        }
    }
    return failure;
}

// Why renewing the sign-in failed, or nothing once it is current.
QString renew(SignInService service, const AppSettings &settings, SecretStore *secrets)
{
    switch (service) {
    case SignInService::OpenAi: {
        const OpenAiAuth renewed =
            OpenAiAuthProvider::forRefinement(secrets, settings.accounts, settings.refinement).resolve(true);
        return renewed.ok ? QString() : renewed.status;
    }
    case SignInService::Anthropic: {
        const AnthropicAuth renewed =
            AnthropicAuthProvider::forRefinement(settings.accounts, settings.refinement).resolve(true);
        return renewed.ok ? QString() : renewed.error;
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
    return check(service, settings, secrets);
}

SignInCheck renewSignIn(SignInService service, AppSettings settings, SecretStore *secrets)
{
    if (const QString failure = readSecrets(service, settings, secrets); !failure.isEmpty()) {
        return {SignInState::Unknown, failure};
    }
    const QString failure = renew(service, settings, secrets);
    SignInCheck renewed = check(service, settings, secrets);
    // The refresh's own error says more than "expired".
    if (renewed.state != SignInState::SignedIn && !failure.isEmpty()) {
        renewed.detail = failure;
    }
    return renewed;
}

bool signInInUse(SignInService service, const AppSettings &settings)
{
    const RefinementSettings &refinement = settings.refinement;
    if (service == SignInService::CliProxy) {
        return refinement.openAiAuthMode == QLatin1String("cliproxy")
            || refinement.anthropicAuthMode == QLatin1String("cliproxy") || !refinement.cliproxyBaseUrl.isEmpty();
    }
    QStringList speech = QStringList{settings.speech.providerId} + settings.speech.fallbackProviderIds;
    QStringList refiners = QStringList{refinement.providerId} + refinement.fallbackProviderIds;
    for (const WritingProfileSettings &profile : refinement.writingProfiles) {
        speech << profile.speechProvider;
        refiners << profile.refinementProvider;
    }
    if (service == SignInService::OpenAi) {
        return speech.contains(QLatin1String("codex")) || refiners.contains(QLatin1String("openai"));
    }
    return speech.contains(QLatin1String("claude")) || refiners.contains(QLatin1String("anthropic"));
}

} // namespace speecher
