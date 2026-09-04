#include "frontend/qt/ProviderCustomRows.h"

#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#include "providers/ClaudeCredentials.h"
#include "providers/CliProxyCredentials.h"
#include "providers/OpenAiAuthProvider.h"
#include "ui/settings/SettingsPageSupport.h"

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPalette>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QThread>
#include <QVBoxLayout>

#include <memory>

namespace speecher {

namespace {

const QString kSettingsKeyAuthMode = QStringLiteral("settings");
const QString kCliProxyAuthMode = QStringLiteral("cliproxy");

} // namespace

ProviderCustomRows::ProviderCustomRows(SettingsStore &settings, SecretStore &secrets)
    : m_settings(settings)
    , m_secrets(secrets)
{
}

SchemaCustomRowFactory ProviderCustomRows::factory()
{
    return [this](const SettingsRow &descriptor,
                  QWidget *parent,
                  std::function<void()> notifyChanged) {
        if (descriptor.id == QStringLiteral("openAiAuthMode")) {
            return makeAuthModeRow(parent, std::move(notifyChanged));
        }
        if (descriptor.id == QStringLiteral("openAiAuth")) {
            return makeCredentialRow(parent, std::move(notifyChanged));
        }
        if (descriptor.id == QStringLiteral("anthropicAuthMode")) {
            return makeAnthropicAuthModeRow(parent, std::move(notifyChanged));
        }
        if (descriptor.id == QStringLiteral("cliproxyBaseUrl")) {
            return makeCliproxyBaseUrlRow(parent, std::move(notifyChanged));
        }
        if (descriptor.id == QStringLiteral("cliproxyApiKey")) {
            return makeCliproxyApiKeyRow(parent, std::move(notifyChanged));
        }
        if (descriptor.id == QStringLiteral("openAiCliproxyAccount")) {
            m_openAiCliproxyAccount = new QComboBox(parent);
            return makeCliproxyAccountRow(m_openAiCliproxyAccount,
                                          m_authMode,
                                          QStringLiteral("codex"),
                                          &m_openAiStoredAccount,
                                          std::move(notifyChanged));
        }
        if (descriptor.id == QStringLiteral("anthropicCliproxyAccount")) {
            m_anthropicCliproxyAccount = new QComboBox(parent);
            return makeCliproxyAccountRow(m_anthropicCliproxyAccount,
                                          m_anthropicAuthMode,
                                          QStringLiteral("claude"),
                                          &m_anthropicStoredAccount,
                                          std::move(notifyChanged));
        }
        return SchemaCustomRow{};
    };
}

SchemaCustomRow ProviderCustomRows::makeAuthModeRow(QWidget *parent,
                                                    std::function<void()> notifyChanged)
{
    m_authMode = new QComboBox(parent);
    // Where the sign-in comes from, in the words a person would use for it.
    m_authMode->addItem(QStringLiteral("Automatic"), QStringLiteral("auto"));
    m_authMode->addItem(QStringLiteral("API key from the Codex app"), QStringLiteral("codex_api_key"));
    m_authMode->addItem(QStringLiteral("ChatGPT sign-in from the Codex app"), QStringLiteral("codex_oauth"));
    m_authMode->addItem(QStringLiteral("API key from the environment"), QStringLiteral("env"));
    m_authMode->addItem(QStringLiteral("API key saved in Speecher"), kSettingsKeyAuthMode);
    m_authMode->addItem(QStringLiteral("CLI Proxy API account"), kCliProxyAuthMode);
    m_authMode->setToolTip(QStringLiteral(
        "API keys only cover text cleanup. Dictation needs the ChatGPT sign-in or a CLI Proxy "
        "API account."));
    QObject::connect(m_authMode,
                     &QComboBox::currentIndexChanged,
                     m_authMode,
                     [this, notifyChanged = std::move(notifyChanged)] {
                         updateCredentialControl();
                         // Logging into CLI Proxy API while the window is open adds
                         // accounts this list hasn't seen yet, so a mode switch
                         // re-reads them from disk rather than trusting whatever
                         // was there when the row was built.
                         if (m_openAiCliproxyAccount) {
                             populateCliproxyAccounts(
                                 m_openAiCliproxyAccount,
                                 QStringLiteral("codex"),
                                 comboSelection(m_openAiCliproxyAccount, m_openAiStoredAccount));
                         }
                         updateAccountTooltips();
                         notifyChanged();
                     });
    return {
        m_authMode,
        [this] { return QVariant(m_authMode->currentData().toString()); },
        [this](const QVariant &value) { settings::selectData(m_authMode, value.toString()); },
    };
}

SchemaCustomRow ProviderCustomRows::makeCredentialRow(QWidget *parent,
                                                      std::function<void()> notifyChanged)
{
    m_credential = new QStackedWidget(parent);
    m_authStatus = new QLabel(m_credential);
    m_apiKey = new QLineEdit(m_credential);
    m_apiKey->setEchoMode(QLineEdit::Password);
    m_apiKey->setPlaceholderText(QStringLiteral("Enter OpenAI API key"));
    m_authStatus->setObjectName(QStringLiteral("openAiAuthStatus"));
    m_authStatus->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_authStatus->setWordWrap(false);
    m_authStatus->setAttribute(Qt::WA_StyledBackground, false);
    m_authStatus->setAutoFillBackground(false);
    m_authStatus->setForegroundRole(QPalette::WindowText);
    m_credential->addWidget(m_authStatus);
    m_credential->addWidget(m_apiKey);

    QObject::connect(m_apiKey, &QLineEdit::textEdited, m_apiKey, [this] { ++m_apiKeyEditRevision; });
    QObject::connect(m_apiKey,
                     &QLineEdit::textChanged,
                     m_apiKey,
                     [notifyChanged = std::move(notifyChanged)] { notifyChanged(); });
    return {m_credential, {}, {}};
}

SchemaCustomRow ProviderCustomRows::makeAnthropicAuthModeRow(QWidget *parent,
                                                             std::function<void()> notifyChanged)
{
    auto *container = new QWidget(parent);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_anthropicAuthMode = new QComboBox(container);
    m_anthropicAuthMode->addItem(QStringLiteral("Claude Code sign-in"), QStringLiteral("oauth"));
    m_anthropicAuthMode->addItem(QStringLiteral("CLI Proxy API account"), kCliProxyAuthMode);
    m_anthropicAuthMode->setToolTip(QStringLiteral(
        "Claude Code sign-in reuses the login from the claude command. CLI Proxy API uses an "
        "account saved by CLI Proxy API."));
    m_anthropicAuthStatus = new QLabel(container);
    m_anthropicAuthStatus->setObjectName(QStringLiteral("anthropicAuthStatus"));
    m_anthropicAuthStatus->setForegroundRole(QPalette::WindowText);
    m_anthropicAuthStatus->setAttribute(Qt::WA_StyledBackground, false);
    m_anthropicAuthStatus->setAutoFillBackground(false);
    layout->addWidget(m_anthropicAuthMode);
    layout->addWidget(m_anthropicAuthStatus);

    QObject::connect(m_anthropicAuthMode,
                     &QComboBox::currentIndexChanged,
                     m_anthropicAuthMode,
                     [this, notifyChanged = std::move(notifyChanged)] {
                         updateAnthropicAuthControl();
                         if (m_anthropicCliproxyAccount) {
                             populateCliproxyAccounts(
                                 m_anthropicCliproxyAccount,
                                 QStringLiteral("claude"),
                                 comboSelection(m_anthropicCliproxyAccount, m_anthropicStoredAccount));
                         }
                         updateAccountTooltips();
                         notifyChanged();
                     });

    return {
        container,
        [this] { return QVariant(m_anthropicAuthMode->currentData().toString()); },
        [this](const QVariant &value) {
            settings::selectData(m_anthropicAuthMode, value.toString());
            updateAnthropicAuthControl();
        },
    };
}

SchemaCustomRow ProviderCustomRows::makeCliproxyBaseUrlRow(
    QWidget *parent,
    std::function<void()> notifyChanged)
{
    m_cliproxyBaseUrl = new QLineEdit(parent);
    m_cliproxyBaseUrl->setPlaceholderText(
        QStringLiteral("http://host:8317 — empty reads local account files"));
    m_cliproxyBaseUrl->setClearButtonEnabled(true);
    QObject::connect(m_cliproxyBaseUrl,
                     &QLineEdit::textEdited,
                     m_cliproxyBaseUrl,
                     [this, notifyChanged = std::move(notifyChanged)] {
                         updateAccountTooltips();
                         updateCredentialControl();
                         notifyChanged();
                     });
    return {
        m_cliproxyBaseUrl,
        [this] { return QVariant(editedCliproxyBaseUrl()); },
        [this](const QVariant &value) {
            m_cliproxyBaseUrl->setText(value.toString());
            updateAccountTooltips();
        },
    };
}

SchemaCustomRow ProviderCustomRows::makeCliproxyApiKeyRow(
    QWidget *parent,
    std::function<void()> notifyChanged)
{
    m_cliproxyApiKey = new QLineEdit(parent);
    m_cliproxyApiKey->setEchoMode(QLineEdit::Password);
    m_cliproxyApiKey->setPlaceholderText(QStringLiteral("CLI Proxy API server api-key"));
    m_cliproxyApiKey->setToolTip(
        QStringLiteral("Stored unencrypted in Speecher's settings file."));
    QObject::connect(m_cliproxyApiKey,
                     &QLineEdit::textEdited,
                     m_cliproxyApiKey,
                     [notifyChanged = std::move(notifyChanged)] {
                         // The credential status doesn't depend on the key text, so
                         // a keystroke doesn't re-resolve it on a thread of its own.
                         notifyChanged();
                     });
    return {
        m_cliproxyApiKey,
        [this] { return QVariant(editedCliproxyApiKey()); },
        [this](const QVariant &value) { m_cliproxyApiKey->setText(value.toString()); },
    };
}

SchemaCustomRow ProviderCustomRows::makeCliproxyAccountRow(QComboBox *account,
                                                           const QComboBox *mode,
                                                           const QString &type,
                                                           QString *stored,
                                                           std::function<void()> notifyChanged)
{
    QObject::connect(account,
                     &QComboBox::currentIndexChanged,
                     account,
                     [this, notifyChanged = std::move(notifyChanged)] {
                         updateCredentialControl();
                         notifyChanged();
                     });
    return {
        account,
        // Silent unless CLI Proxy API is the chosen mode, so picking an account
        // and then changing your mind about the mode persists neither.
        [account, mode, stored] {
            return mode->currentData().toString() == kCliProxyAuthMode
                ? QVariant(comboSelection(account, *stored))
                : QVariant();
        },
        [this, account, type, stored](const QVariant &value) {
            *stored = value.toString();
            populateCliproxyAccounts(account, type, *stored);
        },
    };
}

void ProviderCustomRows::populateCliproxyAccounts(QComboBox *account,
                                                  const QString &type,
                                                  const QString &selected)
{
    const QSignalBlocker blocker(account);
    account->clear();
    const bool serverRouted = !editedCliproxyBaseUrl().isEmpty();
    account->setToolTip(type == QStringLiteral("codex")
                            ? serverRouted
                                ? QStringLiteral("Codex account used for dictation. OpenAI refinement is routed through the configured CLI Proxy API server.")
                                : QStringLiteral("CLI Proxy API Codex account used for dictation and refinement.")
                            : serverRouted
                                ? QStringLiteral("Claude account used for dictation. Anthropic refinement is routed through the configured CLI Proxy API server.")
                                : QStringLiteral("CLI Proxy API Claude account used for dictation and refinement."));
    const QString directory = m_settings.cliproxyOauthDir();
    const QList<CliProxyAccount> accounts = CliProxyCredentials::listAccounts(directory, type);
    // With several accounts and none chosen yet, force an explicit choice
    // instead of silently pinning whichever file sorts first.
    if (selected.isEmpty() && accounts.size() > 1) {
        account->addItem(QStringLiteral("Choose an account…"), QString());
    }
    for (const CliProxyAccount &candidate : accounts) {
        account->addItem(candidate.expired ? candidate.label + QStringLiteral(" (expired)")
                                           : candidate.label,
                         candidate.fileName);
        if (candidate.disabled) {
            settings::setComboItemEnabled(account,
                                          account->count() - 1,
                                          false,
                                          QStringLiteral("Disabled in CLI Proxy API"));
        }
    }
    // Keep a stored selection visible even if its file is currently missing.
    if (!selected.isEmpty() && account->findData(selected) < 0) {
        account->addItem(selected + QStringLiteral(" (missing)"), selected);
    }
    if (account->count() == 0) {
        account->addItem(QStringLiteral("No accounts found"), QString());
        settings::setComboItemEnabled(account, 0, false, directory);
    }
    settings::selectData(account, selected);
}

QString ProviderCustomRows::comboSelection(const QComboBox *account, const QString &stored)
{
    const QString current = account->currentData().toString();
    return current.isEmpty() ? stored : current;
}

QString ProviderCustomRows::editedCliproxyBaseUrl() const
{
    QString base = m_cliproxyBaseUrl ? m_cliproxyBaseUrl->text().trimmed()
                                     : m_settings.cliproxyBaseUrl();
    while (base.endsWith(QLatin1Char('/'))) {
        base.chop(1);
    }
    return base;
}

QString ProviderCustomRows::editedCliproxyApiKey() const
{
    return m_cliproxyApiKey ? m_cliproxyApiKey->text().trimmed()
                            : m_settings.cliproxyApiKey();
}

void ProviderCustomRows::updateAccountTooltips()
{
    const bool serverRouted = !editedCliproxyBaseUrl().isEmpty();
    if (m_openAiCliproxyAccount) {
        m_openAiCliproxyAccount->setToolTip(
            serverRouted
                ? QStringLiteral("Codex account used for dictation. OpenAI refinement is routed through the configured CLI Proxy API server.")
                : QStringLiteral("CLI Proxy API Codex account used for dictation and refinement."));
    }
    if (m_anthropicCliproxyAccount) {
        m_anthropicCliproxyAccount->setToolTip(
            serverRouted
                ? QStringLiteral("Claude account used for dictation. Anthropic refinement is routed through the configured CLI Proxy API server.")
                : QStringLiteral("CLI Proxy API Claude account used for dictation and refinement."));
    }
}

void ProviderCustomRows::loadSecret()
{
    const quint64 editRevision = m_apiKeyEditRevision;
    const QString apiKey = m_secrets.apiKey();
    m_secretLoaded = true;
    if (editRevision == m_apiKeyEditRevision) {
        const QSignalBlocker blocker(m_apiKey);
        m_loadedApiKey = apiKey;
        m_apiKey->setText(apiKey);
    }
    updateCredentialControl();
}

bool ProviderCustomRows::saveSecret()
{
    if (m_settings.openAiAuthMode() == kSettingsKeyAuthMode
        && ((!m_secretLoaded && m_apiKeyEditRevision > 0)
            || (m_secretLoaded && m_apiKey->text().trimmed() != m_loadedApiKey))) {
        if (!m_secrets.saveApiKey(m_apiKey->text().trimmed())) {
            QMessageBox::warning(m_credential,
                                 QStringLiteral("OpenAI key not saved"),
                                 m_secrets.status());
            return false;
        }
        m_loadedApiKey = m_apiKey->text().trimmed();
        m_secretLoaded = true;
    }
    if (m_secretLoaded || m_apiKeyEditRevision > 0) {
        updateCredentialControl();
    }
    return true;
}

void ProviderCustomRows::updateCredentialControl()
{
    const quint64 generation = ++m_authStatusGeneration;
    // The auth-mode row is built before the credential it switches.
    if (!m_credential) {
        return;
    }
    const QString mode = m_authMode->currentData().toString();
    if (mode == kSettingsKeyAuthMode) {
        m_credential->setCurrentWidget(m_apiKey);
        m_apiKey->setPlaceholderText(m_secretLoaded
                                         ? m_secrets.status()
                                         : QStringLiteral("Loading app settings key…"));
        return;
    }
    m_credential->setCurrentWidget(m_authStatus);
    m_authStatus->setText(QStringLiteral("Checking…"));

    const QString account = m_openAiCliproxyAccount
        ? comboSelection(m_openAiCliproxyAccount, m_openAiStoredAccount)
        : m_settings.openAiCliproxyAccount();
    const QString cliproxyDir = m_settings.cliproxyOauthDir();
    const QString settingsApiKey = m_loadedApiKey;
    const QString settingsStatus = m_secrets.status();
    const QString cliproxyBaseUrl = editedCliproxyBaseUrl();
    const QString cliproxyApiKey = editedCliproxyApiKey();
    const auto status = std::make_shared<QString>();
    QThread *thread = QThread::create([mode,
                                       account,
                                       cliproxyDir,
                                       settingsApiKey,
                                       settingsStatus,
                                       cliproxyBaseUrl,
                                       cliproxyApiKey,
                                       status] {
        *status = OpenAiAuthProvider(nullptr,
                                     mode,
                                     account,
                                     cliproxyDir,
                                     settingsApiKey,
                                     settingsStatus,
                                     cliproxyBaseUrl,
                                     cliproxyApiKey)
                      .status();
    });
    QObject::connect(thread,
                     &QThread::finished,
                     m_authStatus,
                     [this, generation, mode, status] {
                         if (generation == m_authStatusGeneration
                             && mode == m_authMode->currentData().toString()) {
                             m_authStatus->setText(*status);
                         }
                     });
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void ProviderCustomRows::updateAnthropicAuthControl()
{
    if (!m_anthropicAuthMode || !m_anthropicAuthStatus) {
        return;
    }
    const bool cliproxy =
        m_anthropicAuthMode->currentData().toString() == kCliProxyAuthMode;
    if (!cliproxy) {
        const ClaudeCredentialResult credentials =
            ClaudeCredentials::load(m_settings.claudeCredentialsPath(), false);
        m_anthropicAuthStatus->setText(
            credentials.ok ? QStringLiteral("Signed in with Claude Code")
                           : credentials.error);
    }
    m_anthropicAuthStatus->setVisible(!cliproxy);
}

} // namespace speecher
