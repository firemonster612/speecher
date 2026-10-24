#include "frontend/quick/host/QuickAccounts.h"

#include "app/ApplicationController.h"
#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#include "providers/ProviderProbe.h"
#include "providers/ProviderSignIn.h"

#include <QDebug>

#include <cstddef>
#include <utility>

namespace speecher {

namespace {

QuickAccounts *g_instance = nullptr;

const struct ServiceRow {
    const char *id;
    const char *name;
    SignInService service;
} serviceRows[] = {
    {"openai", "OpenAI", SignInService::OpenAi},
    {"anthropic", "Anthropic", SignInService::Anthropic},
    {"cliproxy", "CLI Proxy API", SignInService::CliProxy},
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

const QString cliproxyServiceId = QStringLiteral("cliproxy");

template <typename Row, std::size_t count>
const Row *findRow(const Row (&rows)[count], const QString &id)
{
    for (const Row &row : rows) {
        if (id == QLatin1String(row.id)) {
            return &row;
        }
    }
    return nullptr;
}

QString stateName(SignInState state)
{
    switch (state) {
    case SignInState::SignedIn:
        return QStringLiteral("signedIn");
    case SignInState::Expired:
        return QStringLiteral("expired");
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
    return account.expired ? QStringLiteral("expired") : QStringLiteral("signedIn");
}

// The settings a sign-in check reads, other than secrets.
QStringList signInSettings(const RefinementSettings &settings)
{
    return {settings.openAiAuthMode,   settings.openAiCliproxyAccount, settings.anthropicAuthMode,
            settings.anthropicCliproxyAccount, settings.cliproxyOauthDir, settings.cliproxyBaseUrl,
            settings.claudeCredentialsPath};
}

} // namespace

QuickAccounts::QuickAccounts(ApplicationController *controller, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
{
    g_instance = this;
    connect(controller->settings(), &SettingsStore::snapshotApplied, this, &QuickAccounts::settingsSaved);
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
    m_signInSettings = signInSettings(m_controller->settings()->dictationSnapshot().refinement);
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
    const auto check = m_checks.constFind(id);
    const bool checking = m_pending.contains(id) || check == m_checks.cend();
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), QLatin1String(row->name)},
        {QStringLiteral("state"), checking ? QStringLiteral("checking") : stateName(check->state)},
        {QStringLiteral("detail"), check == m_checks.cend() ? QString() : check->detail},
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

QVariantMap QuickAccounts::secretErrors() const
{
    return m_secretErrors;
}

void QuickAccounts::recheck(const QString &id)
{
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
    if (id == cliproxyServiceId) {
        readCliproxyAccounts();
    }
    const quint64 number = ++m_lastCheck;
    m_pending.insert(id, number);
    emit servicesChanged();
    const SignInService service = row->service;
    runProviderProbe<SignInCheck>(
        this,
        this,
        [service, signIn] {
            // This thread's own store, so the keyring is read here and never
            // through the daemon's cache.
            SettingsStore store;
            return signIn(service, store.snapshot().refinement, store.secrets());
        },
        [this, id, number](const SignInCheck &check) {
            if (m_pending.value(id) != number) {
                return;
            }
            m_pending.remove(id);
            m_checks.insert(id, check);
            emit servicesChanged();
        });
}

void QuickAccounts::recheckAll()
{
    for (const ServiceRow &row : serviceRows) {
        recheck(QLatin1String(row.id));
    }
}

void QuickAccounts::readSavedSecrets()
{
    m_secretsChanged.clear();
    runProviderProbe<QVariantMap>(
        this,
        this,
        [] {
            SettingsStore store;
            QVariantMap saved;
            for (const SecretRow &row : secretRows) {
                const bool present = !store.secrets()->secret(row.secret).isEmpty();
                if (store.secrets()->isSecretKnown(row.secret)) {
                    saved.insert(QLatin1String(row.id), present);
                }
            }
            return saved;
        },
        [this](const QVariantMap &saved) {
            for (auto it = saved.cbegin(); it != saved.cend(); ++it) {
                if (!m_secretsChanged.contains(it.key())) {
                    m_savedSecrets.insert(it.key(), it.value());
                }
            }
            emit secretsChanged();
        });
}

void QuickAccounts::readCliproxyAccounts()
{
    const ProviderSignIn signIn(*m_controller->settings());
    QVariantList accounts;
    for (const QString type : {QStringLiteral("codex"), QStringLiteral("claude")}) {
        for (const CliProxyAccount &account : signIn.accounts(type)) {
            accounts.append(QVariantMap{
                {QStringLiteral("id"), account.fileName},
                {QStringLiteral("type"), type},
                {QStringLiteral("label"), account.label},
                {QStringLiteral("state"), accountState(account)},
            });
        }
    }
    if (accounts == m_cliproxyAccounts) {
        return;
    }
    m_cliproxyAccounts = accounts;
    emit cliproxyAccountsChanged();
}

void QuickAccounts::saveSecret(const QString &rowId, const QString &value)
{
    const SecretRow *row = findRow(secretRows, rowId);
    if (!row) {
        qWarning().noquote() << "Accounts.saveSecret:" << rowId << "is not a secret";
        return;
    }
    const QString cleaned = value.trimmed();
    if (cleaned.isEmpty()) {
        return;
    }
    SecretStore *secrets = m_controller->settings()->secrets();
    // The OpenAI key never falls back to the settings file.
    const bool saved = row->secret == SecretStore::Secret::OpenAiApiKey ? secrets->saveApiKey(cleaned)
                                                                        : secrets->saveSecret(row->secret, cleaned);
    finishSecretChange(rowId, saved, true);
}

void QuickAccounts::clearSecret(const QString &rowId)
{
    const SecretRow *row = findRow(secretRows, rowId);
    if (!row) {
        qWarning().noquote() << "Accounts.clearSecret:" << rowId << "is not a secret";
        return;
    }
    finishSecretChange(rowId, m_controller->settings()->secrets()->clearSecret(row->secret), false);
}

void QuickAccounts::finishSecretChange(const QString &rowId, bool done, bool saved)
{
    m_secretsChanged.insert(rowId);
    if (done) {
        m_savedSecrets.insert(rowId, saved);
        m_secretErrors.remove(rowId);
    } else {
        m_secretErrors.insert(rowId, m_controller->settings()->secrets()->lastError());
    }
    emit secretsChanged();
    if (done && m_started) {
        recheckAll();
    }
}

void QuickAccounts::settingsSaved()
{
    if (!m_started) {
        return;
    }
    const QStringList settings = signInSettings(m_controller->settings()->dictationSnapshot().refinement);
    if (settings == m_signInSettings) {
        return;
    }
    m_signInSettings = settings;
    recheckAll();
}

} // namespace speecher
