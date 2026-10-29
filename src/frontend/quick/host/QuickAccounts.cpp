#include "frontend/quick/host/QuickAccounts.h"

#include "app/ApplicationController.h"
#include "app/LocalSetup.h"
#include "app/ProviderAvailability.h"
#include "core/ProviderTable.h"
#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#include "frontend/quick/host/QmlValues.h"
#include "providers/ProviderProbe.h"
#include "providers/ProviderRegistry.h"
#include "providers/ProviderSignIn.h"

#include <QDebug>

#include <algorithm>
#include <utility>

namespace speecher {

namespace {

QuickAccounts *g_instance = nullptr;

const struct ServiceRow {
    const char *id;
    const char *name;
    SignInService service;
    // The refinement provider whose sign-in this is, if any.
    std::optional<ProviderId> provider;
} serviceRows[] = {
    {"openai", "ChatGPT", SignInService::OpenAi, ProviderId::OpenAi},
    {"anthropic", "Claude", SignInService::Anthropic, ProviderId::Anthropic},
    {"cliproxy", "CLI Proxy API", SignInService::CliProxy, std::nullopt},
};

// The schema's secret rows, each one SecretStore entry.
const struct SecretRow {
    const char *id;
    SecretStore::Secret secret;
} secretRows[] = {
    {"openAiApiKey", SecretStore::Secret::OpenAiApiKey},
    {"cliproxyApiKey", SecretStore::Secret::CliproxyApiKey},
    {"speechEndpointApiKey", SecretStore::Secret::SpeechEndpointKey},
    {"refinementEndpointApiKey", SecretStore::Secret::RefinementEndpointKey},
};

const QString cliproxyAccountTypes[] = {QStringLiteral("codex"), QStringLiteral("claude")};

QString stateName(SignInState state)
{
    switch (state) {
    case SignInState::SignedIn:
        return QStringLiteral("signedIn");
    case SignInState::Expired:
        return QStringLiteral("expired");
    case SignInState::Unknown:
        return QStringLiteral("unknown");
    case SignInState::Missing:
        break;
    }
    return QStringLiteral("missing");
}

QString accountState(const CliProxyAccount &account)
{
    if (account.missing) {
        return QStringLiteral("missing");
    }
    if (account.disabled) {
        return QStringLiteral("disabled");
    }
    if (!account.problem.isEmpty()) {
        return QStringLiteral("unusable");
    }
    return account.expired ? QStringLiteral("expired") : QStringLiteral("signedIn");
}

// CLI Proxy API's account files as cliproxyAccounts lists them. Parses each
// file: worker threads only.
QVariantList readCliproxyAccounts(SettingsStore &store)
{
    const ProviderSignIn signIn(store);
    QVariantList accounts;
    for (const QString &type : cliproxyAccountTypes) {
        for (const CliProxyAccount &account : signIn.accounts(type)) {
            accounts.append(QVariantMap{
                {QStringLiteral("id"), account.fileName},
                {QStringLiteral("type"), type},
                {QStringLiteral("label"), account.label},
                {QStringLiteral("state"), accountState(account)},
            });
        }
    }
    return accounts;
}

// What a check's worker hands back.
struct ServiceAnswer {
    SignInCheck signIn;
    // CLI Proxy API's account files, read for its own check only.
    QVariantList accounts;
};

// Which secrets a read found saved, and which wait for a refused clear to be
// retried, by row id; a secret the keyring did not answer for is in neither.
struct SecretPresence {
    QVariantMap saved;
    QVariantMap pendingClears;
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

// Tells the daemon what a check found of a provider's sign-in, for fallbacks
// and `speecher providers`. Only a definite answer counts: an expired token
// dictation may still renew, or a keyring that did not answer, says nothing.
void noteSignIn(ApplicationController *controller, std::optional<ProviderId> provider, const SignInCheck &check)
{
    if (!provider || (check.state != SignInState::SignedIn && check.state != SignInState::Missing)) {
        return;
    }
    controller->providerAvailability()->noteSignIn(providerKey(*provider), check.state == SignInState::SignedIn);
}

// The settings a sign-in check reads, other than secrets.
QStringList signInSettings(const AppSettings &settings)
{
    const AccountSettings &accounts = settings.accounts;
    return {accounts.openAiAuthMode,   accounts.openAiCliproxyAccount, accounts.anthropicAuthMode,
            accounts.anthropicCliproxyAccount, accounts.cliproxyOauthDir, settings.refinement.cliproxyBaseUrl,
            accounts.claudeCredentialsPath};
}

} // namespace

QuickAccounts::QuickAccounts(ApplicationController *controller, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
{
    g_instance = this;
    connect(controller->settings(), &SettingsStore::snapshotApplied, this, &QuickAccounts::settingsSaved);
    // Setup's fallback offer saves providers without a snapshot, so which
    // sign-ins are in use may change with it.
    connect(controller->localSetup(), &LocalSetup::changed, this, &QuickAccounts::servicesChanged);
}

QuickAccounts::~QuickAccounts()
{
    if (g_instance == this) {
        g_instance = nullptr;
    }
}

QuickAccounts *QuickAccounts::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(g_instance, "QuickAccounts::create", "the host makes Accounts before loading QML");
    if (g_instance) {
        QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
        g_instance->start();
    }
    return g_instance;
}

void QuickAccounts::start()
{
    if (std::exchange(m_started, true)) {
        return;
    }
    m_signInSettings = signInSettings(m_controller->settings()->dictationSnapshot());
    readSavedSecrets();
    recheckAll();
}

QVariantList QuickAccounts::services() const
{
    QVariantList services;
    for (const ServiceRow &row : serviceRows) {
        services.append(service(QLatin1String(row.id)));
    }
    return services;
}

QVariantMap QuickAccounts::service(const QString &id) const
{
    const ServiceRow *row = findRow(serviceRows, id);
    if (!row) {
        return {};
    }
    const bool answered = m_checks.contains(id);
    const bool checking = m_pending.contains(id) || !answered;
    const SignInCheck check = m_checks.value(id);
    const bool inUse = signInInUse(row->service, m_controller->settings()->dictationSnapshot());
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), QLatin1String(row->name)},
        {QStringLiteral("state"), checking ? QStringLiteral("checking") : stateName(check.state)},
        {QStringLiteral("found"), answered ? stateName(check.state) : QString()},
        {QStringLiteral("detail"), check.detail},
        {QStringLiteral("server"), check.server},
        {QStringLiteral("directory"), check.directory},
        {QStringLiteral("inUse"), inUse},
    };
}

QVariantList QuickAccounts::cliproxyAccounts() const
{
    return m_cliproxyAccounts;
}

QVariantMap QuickAccounts::savedSecrets() const
{
    return m_savedSecrets;
}

QVariantMap QuickAccounts::pendingClears() const
{
    return m_pendingClears;
}

QVariantMap QuickAccounts::secretErrors() const
{
    return m_secretErrors;
}

void QuickAccounts::recheck(const QString &id)
{
    readSavedSecrets();
    run(id, checkSignIn);
}

void QuickAccounts::renew(const QString &id)
{
    run(id, renewSignIn);
}

void QuickAccounts::run(const QString &id, SignInRun signIn)
{
    const ServiceRow *row = findRow(serviceRows, id);
    if (!row) {
        qWarning().noquote() << "Accounts: no service" << id;
        return;
    }
    const quint64 number = ++m_lastCheck;
    m_pending.insert(id, number);
    emit servicesChanged();
    const SignInService service = row->service;
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
        [this, row, number, renewing = signIn == renewSignIn](const ServiceAnswer &answer) {
            const QString id = QLatin1String(row->id);
            const bool latest = m_pending.value(id) == number;
            if (latest) {
                m_pending.remove(id);
                m_checks.insert(id, answer.signIn);
                if (row->service == SignInService::CliProxy) {
                    showCliproxyAccounts(answer.accounts);
                }
                noteSignIn(m_controller, row->provider, answer.signIn);
                emit servicesChanged();
            }
            if (!renewing) {
                return;
            }
            // Renewing rewrote credentials the others may read: Codex's and
            // Claude's files, or CLI Proxy API's account files. A check that
            // superseded the renewal may have read them before it rewrote
            // them, so the service itself is checked again too. The speech
            // providers dictate with the same credentials.
            for (const ServiceRow &other : serviceRows) {
                if (!latest || &other != row) {
                    run(QLatin1String(other.id), checkSignIn);
                }
            }
            checkSpeechSignIns();
        });
}

void QuickAccounts::recheckAll()
{
    for (const ServiceRow &row : serviceRows) {
        run(QLatin1String(row.id), checkSignIn);
    }
}

void QuickAccounts::checkSpeechSignIns()
{
    m_controller->providerAvailability()->checkSpeechSignIns(*m_controller->providerRegistry(),
                                                             m_controller->settings()->dictationSnapshot());
}

void QuickAccounts::readSavedSecrets()
{
    const quint64 number = ++m_lastSecretRead;
    m_secretsChanged.clear();
    runProviderProbe<SecretPresence>(
        this,
        this,
        [] {
            SettingsStore store;
            SecretPresence presence;
            for (const SecretRow &row : secretRows) {
                const bool present = !store.secrets()->secret(row.secret).isEmpty();
                if (store.secrets()->isSecretKnown(row.secret)) {
                    presence.saved.insert(QLatin1String(row.id), present);
                    presence.pendingClears.insert(QLatin1String(row.id), store.secrets()->deletionPending(row.secret));
                }
            }
            return presence;
        },
        [this, number](const SecretPresence &presence) {
            if (number != m_lastSecretRead) {
                return;
            }
            for (auto it = presence.saved.cbegin(); it != presence.saved.cend(); ++it) {
                if (!m_secretsChanged.contains(it.key())) {
                    m_savedSecrets.insert(it.key(), it.value());
                    m_pendingClears.insert(it.key(), presence.pendingClears.value(it.key()));
                }
            }
            emit secretsChanged();
        });
}

void QuickAccounts::showCliproxyAccounts(const QVariantList &accounts)
{
    if (accounts == m_cliproxyAccounts) {
        return;
    }
    m_cliproxyAccounts = accounts;
    emit cliproxyAccountsChanged();
}

void QuickAccounts::saveSecret(const QString &rowId, const QString &value)
{
    if (!findRow(secretRows, rowId)) {
        qWarning().noquote() << "Accounts.saveSecret:" << rowId << "is not a secret";
        return;
    }
    const QString cleaned = value.trimmed();
    if (!cleaned.isEmpty()) {
        writeSecret(rowId, cleaned);
    }
}

void QuickAccounts::clearSecret(const QString &rowId)
{
    if (!findRow(secretRows, rowId)) {
        qWarning().noquote() << "Accounts.clearSecret:" << rowId << "is not a secret";
        return;
    }
    writeSecret(rowId, {});
}

void QuickAccounts::writeSecret(const QString &rowId, const QString &value)
{
    // The keyring's wait runs the event loop, where QML can save the same
    // secret again. That write waits its turn, so the keyring and the
    // daemon's copy both end with the value asked for last.
    if (m_writes.contains(rowId)) {
        m_writes.insert(rowId, value);
        return;
    }
    const SecretStore::Secret secret = findRow(secretRows, rowId)->secret;
    for (std::optional<QString> next = value; next; next = m_writes.take(rowId)) {
        m_writes.insert(rowId, std::nullopt);
        const bool done = storeSecret(m_controller->settings()->secrets(), secret, *next);
        finishSecretChange(rowId, done, !next->isEmpty());
        // A field that asked for an earlier value meanwhile lets go of what
        // it holds only once the value asked for last is kept.
        if (done && !next->isEmpty() && !m_writes.value(rowId)) {
            emit secretSaved(rowId);
        }
    }
}

void QuickAccounts::finishSecretChange(const QString &rowId, bool done, bool saved)
{
    m_secretsChanged.insert(rowId);
    SecretStore *secrets = m_controller->settings()->secrets();
    m_pendingClears.insert(rowId, secrets->deletionPending(findRow(secretRows, rowId)->secret));
    if (done) {
        m_savedSecrets.insert(rowId, saved);
        m_secretErrors.remove(rowId);
    } else {
        m_secretErrors.insert(rowId,
                              QVariantMap{
                                  {QStringLiteral("action"), saved ? QStringLiteral("save") : QStringLiteral("clear")},
                                  {QStringLiteral("reason"), secrets->lastError()},
                              });
    }
    emit secretsChanged();
    if (done && m_started) {
        recheckAll();
    }
}

void QuickAccounts::settingsSaved(const AppSettings &previous)
{
    if (!m_started) {
        return;
    }
    const AppSettings current = m_controller->settings()->dictationSnapshot();
    if (std::ranges::any_of(serviceRows, [&](const ServiceRow &row) {
            return signInInUse(row.service, previous) != signInInUse(row.service, current);
        })) {
        emit servicesChanged();
    }
    const QStringList settings = signInSettings(current);
    if (settings == m_signInSettings) {
        return;
    }
    m_signInSettings = settings;
    recheckAll();
}

} // namespace speecher
