#include "frontend/qt/ProviderCustomRows.h"

#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#include "providers/OpenAiAuthProvider.h"
#include "ui/settings/SettingsPageSupport.h"

#include <QComboBox>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>

namespace speecher {

namespace {

const QString kSettingsKeyAuthMode = QStringLiteral("settings");

QIcon informationIcon(QWidget *widget)
{
    QIcon icon = QIcon::fromTheme(QStringLiteral("dialog-information-symbolic"));
    if (icon.isNull()) {
        icon = QIcon::fromTheme(QStringLiteral("dialog-information"));
    }
    if (icon.isNull()) {
        icon = widget->style()->standardIcon(QStyle::SP_MessageBoxInformation, nullptr, widget);
    }
    return icon;
}

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
        return SchemaCustomRow{};
    };
}

SchemaCustomRow ProviderCustomRows::makeAuthModeRow(QWidget *parent,
                                                    std::function<void()> notifyChanged)
{
    m_authMode = new QComboBox(parent);
    m_authMode->addItem(QStringLiteral("Automatic"), QStringLiteral("auto"));
    m_authMode->addItem(QStringLiteral("Codex API key"), QStringLiteral("codex_api_key"));
    m_authMode->addItem(QStringLiteral("Codex OAuth"), QStringLiteral("codex_oauth"));
    m_authMode->addItem(QStringLiteral("OPENAI_API_KEY"), QStringLiteral("env"));
    m_authMode->addItem(QStringLiteral("App settings key"), kSettingsKeyAuthMode);
    QObject::connect(m_authMode,
                     &QComboBox::currentIndexChanged,
                     m_authMode,
                     [this, notifyChanged = std::move(notifyChanged)] {
                         updateCredentialControl();
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
    m_authStatus->setObjectName(QStringLiteral("statusText"));
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
    auto *mode = new QComboBox(parent);
    mode->addItem(QStringLiteral("Claude OAuth"), QStringLiteral("oauth"));
    mode->setToolTip(QStringLiteral(
        "Use the existing Claude Code OAuth session for direct Anthropic API routing."));
    QObject::connect(mode,
                     &QComboBox::currentIndexChanged,
                     mode,
                     [notifyChanged = std::move(notifyChanged)] { notifyChanged(); });

    auto *info = new QPushButton(parent);
    info->setIcon(informationIcon(parent));
    info->setIconSize(QSize(14, 14));
    info->setFlat(true);
    info->setCursor(Qt::PointingHandCursor);
    info->setFixedSize(22, 22);
    info->setToolTip(QStringLiteral("How Anthropic OAuth is used."));
    info->setAccessibleName(QStringLiteral("Anthropic auth info"));
    QObject::connect(info, &QPushButton::clicked, info, [info] {
        QMessageBox::information(
            info,
            QStringLiteral("Anthropic auth"),
            QStringLiteral("Speecher reads the existing Claude Code OAuth session from "
                           "~/.claude/.credentials.json and calls the Anthropic Messages API "
                           "directly. It does not start or control a Claude Code agent session."));
    });

    return {
        mode,
        [mode] { return QVariant(mode->currentData().toString()); },
        [mode](const QVariant &value) { settings::selectData(mode, value.toString()); },
        false,
        info,
    };
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
        && (m_secretLoaded || m_apiKeyEditRevision > 0)) {
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

bool ProviderCustomRows::hasSecretChanges() const
{
    return m_authMode->currentData().toString() == kSettingsKeyAuthMode
        && ((!m_secretLoaded && m_apiKeyEditRevision > 0)
            || (m_secretLoaded && m_apiKey->text().trimmed() != m_loadedApiKey));
}

void ProviderCustomRows::updateCredentialControl()
{
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
    m_authStatus->setText(OpenAiAuthProvider(&m_secrets, mode).status());
    m_credential->setCurrentWidget(m_authStatus);
}

} // namespace speecher
