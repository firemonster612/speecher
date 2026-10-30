#include "frontend/quick/host/QuickAccounts.h"

#include "app/Accounts.h"
#include "app/ApplicationController.h"
#include "app/LocalSetup.h"
#include "core/SettingsStore.h"
#include "frontend/quick/host/QmlValues.h"

#include <QDebug>

#include <algorithm>

namespace speecher {

namespace {

QuickAccounts *g_instance = nullptr;

const struct ServiceRow {
    const char *id;
    const char *name;
    SignInService service;
} serviceRows[] = {
    {"openai", "ChatGPT", SignInService::OpenAi},
    {"anthropic", "Claude", SignInService::Anthropic},
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

// A secret map by row id, from one keyed by secret; a secret it lacks is
// absent.
template <typename Value, typename Convert>
QVariantMap bySecretRow(const QMap<SecretStore::Secret, Value> &values, Convert convert)
{
    QVariantMap rows;
    for (const SecretRow &row : secretRows) {
        if (const auto value = values.constFind(row.secret); value != values.cend()) {
            rows.insert(QLatin1String(row.id), convert(*value));
        }
    }
    return rows;
}

} // namespace

QuickAccounts::QuickAccounts(ApplicationController *controller, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
    , m_accounts(controller->accounts())
{
    g_instance = this;
    connect(m_accounts, &Accounts::signInsChanged, this, &QuickAccounts::servicesChanged);
    connect(m_accounts, &Accounts::cliproxyAccountsChanged, this, &QuickAccounts::cliproxyAccountsChanged);
    connect(m_accounts, &Accounts::secretsChanged, this, &QuickAccounts::secretsChanged);
    connect(m_accounts, &Accounts::secretSaved, this, [this](SecretStore::Secret secret) {
        for (const SecretRow &row : secretRows) {
            if (row.secret == secret) {
                emit secretSaved(QLatin1String(row.id));
            }
        }
    });
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
        g_instance->m_accounts->start();
    }
    return g_instance;
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
    const std::optional<SignInCheck> found = m_accounts->signIn(row->service);
    const bool checking = m_accounts->checking(row->service) || !found;
    const SignInCheck check = found.value_or(SignInCheck{});
    const bool inUse = signInInUse(row->service, m_controller->settings()->dictationSnapshot());
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), QLatin1String(row->name)},
        {QStringLiteral("state"), checking ? QStringLiteral("checking") : stateName(check.state)},
        {QStringLiteral("found"), found ? stateName(check.state) : QString()},
        {QStringLiteral("detail"), check.detail},
        {QStringLiteral("server"), check.server},
        {QStringLiteral("directory"), check.directory},
        {QStringLiteral("inUse"), inUse},
    };
}

QVariantList QuickAccounts::cliproxyAccounts() const
{
    QVariantList accounts;
    for (const Accounts::CliProxyAccountFile &file : m_accounts->cliproxyAccounts()) {
        accounts.append(QVariantMap{
            {QStringLiteral("id"), file.account.fileName},
            {QStringLiteral("type"), file.type},
            {QStringLiteral("label"), file.account.label},
            {QStringLiteral("state"), accountState(file.account)},
        });
    }
    return accounts;
}

QVariantMap QuickAccounts::savedSecrets() const
{
    return bySecretRow(m_accounts->savedSecrets(), [](bool saved) { return saved; });
}

QVariantMap QuickAccounts::pendingClears() const
{
    return bySecretRow(m_accounts->pendingClears(), [](bool pending) { return pending; });
}

QVariantMap QuickAccounts::secretErrors() const
{
    return bySecretRow(m_accounts->writeFailures(), [](const Accounts::WriteFailure &failure) {
        return QVariantMap{
            {QStringLiteral("action"), failure.clearing ? QStringLiteral("clear") : QStringLiteral("save")},
            {QStringLiteral("reason"), failure.reason},
        };
    });
}

void QuickAccounts::recheck(const QString &id)
{
    if (const ServiceRow *row = findRow(serviceRows, id)) {
        m_accounts->recheck(row->service);
    } else {
        qWarning().noquote() << "Accounts: no service" << id;
    }
}

void QuickAccounts::renew(const QString &id)
{
    if (const ServiceRow *row = findRow(serviceRows, id)) {
        m_accounts->renew(row->service);
    } else {
        qWarning().noquote() << "Accounts: no service" << id;
    }
}

void QuickAccounts::saveSecret(const QString &rowId, const QString &value)
{
    if (const SecretRow *row = findRow(secretRows, rowId)) {
        m_accounts->saveSecret(row->secret, value);
    } else {
        qWarning().noquote() << "Accounts.saveSecret:" << rowId << "is not a secret";
    }
}

void QuickAccounts::clearSecret(const QString &rowId)
{
    if (const SecretRow *row = findRow(secretRows, rowId)) {
        m_accounts->clearSecret(row->secret);
    } else {
        qWarning().noquote() << "Accounts.clearSecret:" << rowId << "is not a secret";
    }
}

void QuickAccounts::settingsSaved(const AppSettings &previous)
{
    const AppSettings current = m_controller->settings()->dictationSnapshot();
    if (std::ranges::any_of(serviceRows, [&](const ServiceRow &row) {
            return signInInUse(row.service, previous) != signInInUse(row.service, current);
        })) {
        emit servicesChanged();
    }
}

} // namespace speecher
