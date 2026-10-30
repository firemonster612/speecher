#include "app/Accounts.h"

#include "app/ProviderAvailability.h"
#include "core/ProviderTable.h"
#include "core/SettingsStore.h"
#include "providers/ProviderProbe.h"
#include "providers/ProviderSignIn.h"

#include <utility>

namespace speecher {

namespace {

constexpr SignInService services[] = {SignInService::OpenAi, SignInService::Anthropic, SignInService::CliProxy};

// The refinement provider whose sign-in the service is, if any.
std::optional<ProviderId> refinementProvider(SignInService service)
{
    switch (service) {
    case SignInService::OpenAi:
        return ProviderId::OpenAi;
    case SignInService::Anthropic:
        return ProviderId::Anthropic;
    case SignInService::CliProxy:
        break;
    }
    return std::nullopt;
}

const QString cliproxyAccountTypes[] = {QStringLiteral("codex"), QStringLiteral("claude")};

// CLI Proxy API's account files. Parses each file: worker threads only.
QList<Accounts::CliProxyAccountFile> readCliproxyAccounts(SettingsStore &store)
{
    const ProviderSignIn signIn(store);
    QList<Accounts::CliProxyAccountFile> files;
    for (const QString &type : cliproxyAccountTypes) {
        for (const CliProxyAccount &account : signIn.accounts(type)) {
            files.append({type, account});
        }
    }
    return files;
}

// What a check's worker hands back.
struct ServiceAnswer {
    SignInCheck signIn;
    // CLI Proxy API's account files, read for its own check only.
    QList<Accounts::CliProxyAccountFile> accounts;
};

// Which secrets a read found saved, and which wait for a refused clear to be
// retried; a secret the keyring did not answer for is in neither.
struct SecretPresence {
    QMap<SecretStore::Secret, bool> saved;
    QMap<SecretStore::Secret, bool> pendingClears;
};

// Saves value, or clears the secret when it is empty. The OpenAI key never
// falls back to the settings file, so it goes through saveApiKey either way.
bool storeSecret(SecretStore *secrets, SecretStore::Secret secret, const QString &value)
{
    if (secret == SecretStore::Secret::OpenAiApiKey) {
        return secrets->saveApiKey(value);
    }
    return value.isEmpty() ? secrets->clearSecret(secret) : secrets->saveSecret(secret, value);
}

// Tells ProviderAvailability what a check found of a provider's sign-in, for
// fallbacks and `speecher providers`. Only a definite answer counts: an
// expired token dictation may still renew, or a keyring that did not answer,
// says nothing.
void noteSignIn(ProviderAvailability &availability, std::optional<ProviderId> provider, const SignInCheck &check)
{
    if (!provider || (check.state != SignInState::SignedIn && check.state != SignInState::Missing)) {
        return;
    }
    availability.noteSignIn(providerKey(*provider), check.state == SignInState::SignedIn);
}

// The settings a sign-in check reads, other than secrets.
QStringList signInSettings(const AppSettings &settings)
{
    const AccountSettings &accounts = settings.accounts;
    return {openAiAuthModeName(accounts.openAiAuthMode), accounts.openAiCliproxyAccount,
            anthropicAuthModeName(accounts.anthropicAuthMode),
            accounts.anthropicCliproxyAccount, accounts.cliproxyOauthDir, settings.refinement.cliproxyBaseUrl,
            accounts.claudeCredentialsPath};
}

} // namespace

Accounts::Accounts(SettingsStore &settings, ProviderAvailability &availability, ProviderRegistry &registry, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_availability(availability)
    , m_registry(registry)
{
    connect(&settings, &SettingsStore::snapshotApplied, this, &Accounts::settingsSaved);
}

void Accounts::start()
{
    if (std::exchange(m_started, true)) {
        return;
    }
    m_signInSettings = signInSettings(m_settings.dictationSnapshot());
    readSavedSecrets();
    recheckAll();
}

std::optional<SignInCheck> Accounts::signIn(SignInService service) const
{
    const auto check = m_checks.constFind(service);
    if (check == m_checks.cend()) {
        return std::nullopt;
    }
    return *check;
}

void Accounts::recheck(SignInService service)
{
    readSavedSecrets();
    run(service, checkSignIn);
}

void Accounts::renew(SignInService service)
{
    run(service, renewSignIn);
}

void Accounts::run(SignInService service, SignInRun signIn)
{
    const quint64 number = ++m_lastCheck;
    m_pending.insert(service, number);
    emit signInsChanged();
    runProviderProbe<ServiceAnswer>(
        this,
        this,
        [service, signIn] {
            // This thread's own store, so the keyring is read here and never
            // through the daemon's cache.
            SettingsStore store;
            const AppSettings settings = store.dictationSnapshot();
            ServiceAnswer answer{signIn(service, settings, store.secrets())};
            if (service == SignInService::CliProxy) {
                answer.accounts = readCliproxyAccounts(store);
            }
            return answer;
        },
        [this, service, number, renewing = signIn == renewSignIn](const ServiceAnswer &answer) {
            const bool latest = m_pending.value(service) == number;
            if (latest) {
                m_pending.remove(service);
                m_checks.insert(service, answer.signIn);
                if (service == SignInService::CliProxy && answer.accounts != m_cliproxyAccounts) {
                    m_cliproxyAccounts = answer.accounts;
                    emit cliproxyAccountsChanged();
                }
                noteSignIn(m_availability, refinementProvider(service), answer.signIn);
                emit signInsChanged();
            }
            if (!renewing) {
                return;
            }
            // Renewing rewrote credentials the others may read: Codex's and
            // Claude's files, or CLI Proxy API's account files. A check that
            // superseded the renewal may have read them before it rewrote
            // them, so the service itself is checked again too. The speech
            // providers dictate with the same credentials.
            for (const SignInService other : services) {
                if (!latest || other != service) {
                    run(other, checkSignIn);
                }
            }
            m_availability.checkSpeechSignIns(m_registry, m_settings.dictationSnapshot());
        });
}

void Accounts::recheckAll()
{
    for (const SignInService service : services) {
        run(service, checkSignIn);
    }
}

void Accounts::readSavedSecrets()
{
    const quint64 number = ++m_lastSecretRead;
    m_secretsWritten.clear();
    runProviderProbe<SecretPresence>(
        this,
        this,
        [] {
            SettingsStore store;
            SecretPresence presence;
            // Every secret SecretStore keeps; the last one sizes the enum.
            for (int i = 0; i <= int(SecretStore::Secret::RefinementEndpointKey); ++i) {
                const auto secret = SecretStore::Secret(i);
                const bool present = !store.secrets()->secret(secret).isEmpty();
                if (store.secrets()->isSecretKnown(secret)) {
                    presence.saved.insert(secret, present);
                    presence.pendingClears.insert(secret, store.secrets()->deletionPending(secret));
                }
            }
            return presence;
        },
        [this, number](const SecretPresence &presence) {
            if (number != m_lastSecretRead) {
                return;
            }
            for (auto it = presence.saved.cbegin(); it != presence.saved.cend(); ++it) {
                if (!m_secretsWritten.contains(it.key())) {
                    m_savedSecrets.insert(it.key(), it.value());
                    m_pendingClears.insert(it.key(), presence.pendingClears.value(it.key()));
                }
            }
            emit secretsChanged();
        });
}

void Accounts::saveSecret(SecretStore::Secret secret, const QString &value)
{
    const QString cleaned = value.trimmed();
    if (!cleaned.isEmpty()) {
        write(secret, cleaned);
    }
}

void Accounts::clearSecret(SecretStore::Secret secret)
{
    write(secret, {});
}

void Accounts::write(SecretStore::Secret secret, const QString &value)
{
    // The keyring's wait runs the event loop, where the UI can save the same
    // secret again. That write waits its turn, so the keyring and the
    // daemon's copy both end with the value asked for last.
    if (m_writes.contains(secret)) {
        m_writes.insert(secret, value);
        return;
    }
    for (std::optional<QString> next = value; next; next = m_writes.take(secret)) {
        m_writes.insert(secret, std::nullopt);
        const bool done = storeSecret(m_settings.secrets(), secret, *next);
        finishWrite(secret, done, !next->isEmpty());
        // A field that asked for an earlier value meanwhile lets go of what
        // it holds only once the value asked for last is kept.
        if (done && !next->isEmpty() && !m_writes.value(secret)) {
            emit secretSaved(secret);
        }
    }
}

void Accounts::finishWrite(SecretStore::Secret secret, bool done, bool saved)
{
    m_secretsWritten.append(secret);
    SecretStore *secrets = m_settings.secrets();
    m_pendingClears.insert(secret, secrets->deletionPending(secret));
    if (done) {
        m_savedSecrets.insert(secret, saved);
        m_writeFailures.remove(secret);
    } else {
        m_writeFailures.insert(secret, {!saved, secrets->lastError()});
    }
    emit secretsChanged();
    if (done && m_started) {
        recheckAll();
    }
}

void Accounts::settingsSaved()
{
    if (!m_started) {
        return;
    }
    const QStringList settings = signInSettings(m_settings.dictationSnapshot());
    if (settings == m_signInSettings) {
        return;
    }
    m_signInSettings = settings;
    recheckAll();
}

} // namespace speecher
