#include "ui/settings/ProviderSettingsPage.h"

#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#include "providers/CliProxyCredentials.h"
#include "providers/OpenAiAuthProvider.h"
#include "ui/settings/SettingsPageSupport.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStyle>
#include <QThread>
#include <QVBoxLayout>
#include <QtMath>

#include <memory>

namespace speecher {

static QIcon informationIcon(QWidget *widget)
{
    QIcon icon = QIcon::fromTheme(QStringLiteral("dialog-information-symbolic"));
    if (icon.isNull()) {
        icon = QIcon::fromTheme(QStringLiteral("dialog-information"));
    }
    if (icon.isNull() && widget) {
        icon = widget->style()->standardIcon(QStyle::SP_MessageBoxInformation, nullptr, widget);
    }
    return icon;
}

static QWidget *makeAnthropicModelControl(QComboBox *model, QLabel *warning, QWidget **warningRowOut, QWidget *parent)
{
    auto *control = new QWidget(parent);
    auto *layout = new QVBoxLayout(control);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto *warningRow = new QWidget(control);
    auto *warningLayout = new QHBoxLayout(warningRow);
    warningLayout->setContentsMargins(0, 0, 0, 0);
    warningLayout->setSpacing(5);
    warningRow->setFixedHeight(18);

    auto *icon = new QLabel(warningRow);
    icon->setAlignment(Qt::AlignCenter);
    icon->setFixedSize(18, 18);
    icon->setPixmap(parent->style()
                        ->standardIcon(QStyle::SP_MessageBoxWarning, nullptr, parent)
                        .pixmap(16, 16));

    QFont warningFont = warning->font();
    if (warningFont.pointSize() > 0) {
        warningFont.setPointSize(qMax(warningFont.pointSize() - 2, 8));
    } else {
        warningFont.setPixelSize(qMax(warningFont.pixelSize() - 2, 11));
    }
    warning->setFont(warningFont);
    warning->setWordWrap(false);
    warning->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    warning->setMinimumWidth(0);
    warning->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    warning->setFixedHeight(18);
    warningLayout->addWidget(icon, 0, Qt::AlignVCenter);
    warningLayout->addWidget(warning, 1, Qt::AlignVCenter);
    layout->addWidget(model);
    layout->addWidget(warningRow);

    if (warningRowOut) {
        *warningRowOut = warningRow;
    }
    warningRow->setVisible(false);
    return control;
}

ProviderSettingsPage::ProviderSettingsPage(SettingsStore &settings, SecretStore &secrets, QWidget *parent)
    : QScrollArea(parent)
    , m_settings(settings)
    , m_secrets(secrets)
    , m_openAiModel(new QComboBox(this))
    , m_openAiEffort(new QComboBox(this))
    , m_anthropicModel(new QComboBox(this))
    , m_anthropicEffort(new QComboBox(this))
    , m_authMode(new QComboBox(this))
    , m_anthropicAuthMode(new QComboBox(this))
    , m_speechAuthMode(new QComboBox(this))
    , m_openAiCliproxyAccount(new QComboBox(this))
    , m_anthropicCliproxyAccount(new QComboBox(this))
    , m_authControl(new QStackedWidget(this))
    , m_authStatus(new QLabel(this))
    , m_anthropicWarning(new QLabel(this))
    , m_apiKey(new QLineEdit(this))
{
    for (const QString &model : {
             QStringLiteral("gpt-5.6-luna"),
             QStringLiteral("gpt-5.6-terra"),
             QStringLiteral("gpt-5.6-sol"),
             QStringLiteral("gpt-5.5"),
             QStringLiteral("gpt-5.4-nano"),
             QStringLiteral("gpt-5.4-mini"),
             QStringLiteral("gpt-5.4"),
         }) {
        m_openAiModel->addItem(model, model);
    }
    m_openAiModel->setEditable(true);
    m_openAiModel->setInsertPolicy(QComboBox::NoInsert);
    m_openAiModel->setMaxVisibleItems(6);
    m_openAiModel->setMinimumContentsLength(16);
    m_openAiModel->setToolTip(QStringLiteral("Defaults to gpt-5.6-luna with no reasoning effort. Select another model or type another model ID."));
    m_openAiModel->view()->setMouseTracking(true);
    if (m_openAiModel->lineEdit()) {
        m_openAiModel->lineEdit()->setClearButtonEnabled(true);
    }
    m_openAiEffort->addItem(QStringLiteral("None"), QStringLiteral("none"));
    m_openAiEffort->addItem(QStringLiteral("Low"), QStringLiteral("low"));
    m_openAiEffort->addItem(QStringLiteral("Medium"), QStringLiteral("medium"));
    m_openAiEffort->addItem(QStringLiteral("High"), QStringLiteral("high"));
    m_openAiEffort->addItem(QStringLiteral("Extra high"), QStringLiteral("xhigh"));
    m_openAiEffort->setToolTip(QStringLiteral("OpenAI Responses reasoning.effort. Supported values vary by model."));
    const QList<QPair<QString, QString>> anthropicModels{
        {QStringLiteral("Claude Opus 4.8"), QStringLiteral("claude-opus-4-8")},
        {QStringLiteral("Claude Sonnet 4.6"), QStringLiteral("claude-sonnet-4-6")},
        {QStringLiteral("Claude Haiku 4.5"), QStringLiteral("claude-haiku-4-5-20251001")},
    };
    for (const auto &model : anthropicModels) {
        m_anthropicModel->addItem(model.first, model.second);
    }
    m_anthropicModel->setEditable(true);
    m_anthropicModel->setInsertPolicy(QComboBox::NoInsert);
    m_anthropicModel->setMaxVisibleItems(8);
    m_anthropicModel->setMinimumContentsLength(24);
    m_anthropicModel->setToolTip(QStringLiteral("Defaults to Claude Sonnet 4.6. Select a model or type another model ID."));
    m_anthropicModel->view()->setMouseTracking(true);
    if (m_anthropicModel->lineEdit()) {
        m_anthropicModel->lineEdit()->setClearButtonEnabled(true);
    }
    m_anthropicEffort->addItem(QStringLiteral("Low"), QStringLiteral("low"));
    m_anthropicEffort->addItem(QStringLiteral("Medium"), QStringLiteral("medium"));
    m_anthropicEffort->addItem(QStringLiteral("High"), QStringLiteral("high"));
    m_anthropicEffort->addItem(QStringLiteral("Extra high"), QStringLiteral("xhigh"));
    m_anthropicEffort->addItem(QStringLiteral("Max"), QStringLiteral("max"));
    m_anthropicEffort->setToolTip(QStringLiteral("Claude effort. Anthropic API support depends on the selected model."));
    m_authMode->addItem(QStringLiteral("Automatic"), QStringLiteral("auto"));
    m_authMode->addItem(QStringLiteral("Codex API key"), QStringLiteral("codex_api_key"));
    m_authMode->addItem(QStringLiteral("Codex OAuth"), QStringLiteral("codex_oauth"));
    m_authMode->addItem(QStringLiteral("OPENAI_API_KEY"), QStringLiteral("env"));
    m_authMode->addItem(QStringLiteral("App settings key"), QStringLiteral("settings"));
    m_authMode->addItem(QStringLiteral("CLI Proxy API"), QStringLiteral("cliproxy"));
    m_anthropicAuthMode->addItem(QStringLiteral("Claude OAuth"), QStringLiteral("oauth"));
    m_anthropicAuthMode->addItem(QStringLiteral("CLI Proxy API"), QStringLiteral("cliproxy"));
    m_anthropicAuthMode->setToolTip(QStringLiteral("Claude OAuth uses the existing Claude Code session. CLI Proxy API uses an OAuth account saved by CLI Proxy API."));
    m_openAiCliproxyAccount->setObjectName(QStringLiteral("openAiCliproxyAccount"));
    m_anthropicCliproxyAccount->setObjectName(QStringLiteral("anthropicCliproxyAccount"));
    m_openAiCliproxyAccount->setToolTip(QStringLiteral("CLI Proxy API Codex account used for refinement."));
    m_anthropicCliproxyAccount->setToolTip(QStringLiteral("CLI Proxy API Claude account used for refinement."));
    m_apiKey->setEchoMode(QLineEdit::Password);
    m_apiKey->setPlaceholderText(QStringLiteral("Enter OpenAI API key"));
    m_authControl->addWidget(m_authStatus);
    m_authControl->addWidget(m_apiKey);
    m_authControl->addWidget(m_openAiCliproxyAccount);

    m_authStatus->setObjectName(QStringLiteral("statusText"));
    m_authStatus->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_authStatus->setWordWrap(false);
    m_authStatus->setAttribute(Qt::WA_StyledBackground, false);
    m_authStatus->setAutoFillBackground(false);
    m_anthropicWarning->setObjectName(QStringLiteral("statusText"));
    m_anthropicWarning->setForegroundRole(QPalette::WindowText);
    auto *anthropicInfoButton = new QPushButton(this);
    anthropicInfoButton->setIcon(informationIcon(this));
    anthropicInfoButton->setIconSize(QSize(14, 14));
    anthropicInfoButton->setFlat(true);
    anthropicInfoButton->setCursor(Qt::PointingHandCursor);
    anthropicInfoButton->setFixedSize(22, 22);
    anthropicInfoButton->setToolTip(QStringLiteral("How Anthropic auth is used."));
    anthropicInfoButton->setAccessibleName(QStringLiteral("Anthropic auth info"));
    m_authStatus->setForegroundRole(QPalette::WindowText);

    auto *title = settings::makePageTitle(QStringLiteral("Providers"), this);
    auto *openAiCard = settings::makeSettingsCard(this);
    auto *openAiLayout = settings::cardFormLayout(openAiCard);
    auto *anthropicCard = settings::makeSettingsCard(this);
    auto *anthropicLayout = settings::cardFormLayout(anthropicCard);
    auto *openAiAuthCard = settings::makeSettingsCard(this);
    auto *openAiAuthLayout = settings::cardFormLayout(openAiAuthCard);
    auto *anthropicAuthCard = settings::makeSettingsCard(this);
    auto *anthropicAuthCardLayout = settings::cardFormLayout(anthropicAuthCard);
    auto *speechAuthCard = settings::makeSettingsCard(this);
    auto *speechAuthLayout = settings::cardFormLayout(speechAuthCard);

    settings::addSectionRow(openAiLayout, QStringLiteral("OpenAI"), openAiCard);
    settings::addRow(openAiLayout, settings::makeRow(QStringLiteral("OpenAI model"), QStringLiteral("Model used for refinement."), m_openAiModel, openAiCard), openAiCard);
    settings::addRow(openAiLayout, settings::makeRow(QStringLiteral("OpenAI effort"), QStringLiteral("Reasoning effort used for refinement."), m_openAiEffort, openAiCard), openAiCard);
    settings::addSectionRow(openAiAuthLayout, QStringLiteral("OpenAI"), openAiAuthCard);
    settings::addRow(openAiAuthLayout, settings::makeRow(QStringLiteral("OpenAI auth mode"), QStringLiteral("Credential source used for refinement."), m_authMode, openAiAuthCard), openAiAuthCard);
    settings::addRow(openAiAuthLayout, settings::makeRow(QStringLiteral("OpenAI auth"), QStringLiteral("Current credential source, app settings key, or CLI Proxy API account."), m_authControl, openAiAuthCard), openAiAuthCard, false);

    m_speechAuthMode->setObjectName(QStringLiteral("speechAuthMode"));
    m_speechAuthMode->addItem(QStringLiteral("Provider default"), QStringLiteral("default"));
    m_speechAuthMode->addItem(QStringLiteral("CLI Proxy API"), QStringLiteral("cliproxy"));
    m_speechAuthMode->setToolTip(QStringLiteral(
        "Provider default uses the Claude Code or Codex CLI login. CLI Proxy API uses the OAuth "
        "account files CLI Proxy API keeps locally, with the accounts selected on this page."));
    settings::addSectionRow(speechAuthLayout, QStringLiteral("Speech"), speechAuthCard);
    settings::addRow(speechAuthLayout,
                     settings::makeRow(QStringLiteral("Speech credentials"),
                                       QStringLiteral("Credential source used by the transcription service."),
                                       m_speechAuthMode,
                                       speechAuthCard),
                     speechAuthCard);

    settings::addSectionRow(anthropicLayout, QStringLiteral("Anthropic"), anthropicCard);
    settings::addRow(anthropicLayout,
                     settings::makeRow(QStringLiteral("Claude model"),
                                       QStringLiteral("Model used for Anthropic refinement."),
                                       makeAnthropicModelControl(m_anthropicModel, m_anthropicWarning, &m_anthropicWarningRow, anthropicCard),
                                       anthropicCard),
                     anthropicCard);
    settings::addRow(anthropicLayout,
                     settings::makeRow(QStringLiteral("Claude effort"),
                                       QStringLiteral("Token spend and reasoning depth for Anthropic refinement."),
                                       m_anthropicEffort,
                                       anthropicCard),
                     anthropicCard);
    auto *anthropicAuthControl = new QWidget(anthropicAuthCard);
    auto *anthropicAuthLayout = new QVBoxLayout(anthropicAuthControl);
    anthropicAuthLayout->setContentsMargins(0, 0, 0, 0);
    anthropicAuthLayout->setSpacing(6);
    anthropicAuthLayout->addWidget(m_anthropicAuthMode);
    anthropicAuthLayout->addWidget(m_anthropicCliproxyAccount);
    m_anthropicCliproxyAccount->setVisible(false);
    settings::addSectionRow(anthropicAuthCardLayout, QStringLiteral("Anthropic"), anthropicAuthCard);
    settings::addRow(anthropicAuthCardLayout,
                     settings::makeRow(QStringLiteral("Anthropic auth"),
                                       QStringLiteral("How Speecher sends refinement requests to Claude."),
                                       anthropicAuthControl,
                                       anthropicAuthCard,
                                       anthropicInfoButton),
                     anthropicAuthCard,
                     false);

    m_cliproxyCard = settings::makeSettingsCard(this);
    auto *cliproxyLayout = settings::cardFormLayout(qobject_cast<QFrame *>(m_cliproxyCard));
    m_cliproxyBaseUrl = new QLineEdit(this);
    m_cliproxyBaseUrl->setObjectName(QStringLiteral("cliproxyBaseUrl"));
    m_cliproxyBaseUrl->setPlaceholderText(QStringLiteral("http://host:8317 — empty reads local account files"));
    m_cliproxyBaseUrl->setClearButtonEnabled(true);
    m_cliproxyApiKey = new QLineEdit(this);
    m_cliproxyApiKey->setObjectName(QStringLiteral("cliproxyApiKey"));
    m_cliproxyApiKey->setEchoMode(QLineEdit::Password);
    m_cliproxyApiKey->setPlaceholderText(QStringLiteral("CLI Proxy API server api-key"));
    settings::addSectionRow(cliproxyLayout, QStringLiteral("CLI Proxy API"), m_cliproxyCard);
    settings::addRow(cliproxyLayout,
                     settings::makeRow(QStringLiteral("Server URL"),
                                       QStringLiteral("CLI Proxy API server to send refinement through. When set, the server "
                                                      "picks and refreshes accounts; leave empty to read its local token files."),
                                       m_cliproxyBaseUrl,
                                       m_cliproxyCard),
                     m_cliproxyCard);
    settings::addRow(cliproxyLayout,
                     settings::makeRow(QStringLiteral("Server API key"),
                                       QStringLiteral("An entry from the server's api-keys list. Required when the server URL is set."),
                                       m_cliproxyApiKey,
                                       m_cliproxyCard),
                     m_cliproxyCard);

    auto *note = new QLabel(QStringLiteral("Automatic OpenAI auth follows the Codex auth mode when available, then falls back to Codex API key, Codex OAuth, OPENAI_API_KEY, and the app settings key. Codex OAuth uses the ChatGPT Codex backend. The app settings key is stored in the desktop keyring through QtKeychain when available. CLI Proxy API auth reads the OAuth accounts from CLI Proxy API's auth directory (auto-detected) and uses the selected account's token directly — or, with a server URL configured, sends refinement through the CLI Proxy API server itself."), this);
    note->setObjectName(QStringLiteral("noteText"));
    note->setWordWrap(true);
    note->setForegroundRole(QPalette::WindowText);
    note->setAttribute(Qt::WA_StyledBackground, false);

    m_modelsContent = new QWidget(this);
    auto *modelsLayout = new QVBoxLayout(m_modelsContent);
    modelsLayout->setContentsMargins(0, 0, 0, 0);
    modelsLayout->setSpacing(0);
    modelsLayout->addWidget(openAiCard);
    modelsLayout->addSpacing(settings::groupGap());
    modelsLayout->addWidget(anthropicCard);

    m_authContent = new QWidget(this);
    auto *authLayout = new QVBoxLayout(m_authContent);
    authLayout->setContentsMargins(0, 0, 0, 0);
    authLayout->setSpacing(0);
    authLayout->addWidget(speechAuthCard);
    authLayout->addSpacing(settings::groupGap());
    authLayout->addWidget(openAiAuthCard);
    authLayout->addSpacing(settings::groupGap());
    authLayout->addWidget(anthropicAuthCard);
    authLayout->addSpacing(settings::groupGap());
    authLayout->addWidget(m_cliproxyCard);
    authLayout->addSpacing(settings::relatedSpacing());
    authLayout->addWidget(note);

    auto *pageLayout = settings::makeSettingsPage(this);
    pageLayout->setSpacing(0);
    pageLayout->addWidget(title);
    pageLayout->addSpacing(settings::sectionGap());
    pageLayout->addWidget(m_modelsContent);
    pageLayout->addSpacing(settings::groupGap());
    pageLayout->addWidget(m_authContent);
    pageLayout->addStretch();

    connect(m_cliproxyBaseUrl, &QLineEdit::textEdited, this, [this] {
        updateAuthControl();
        updateAnthropicAuthControl();
        emit changed();
    });
    connect(m_cliproxyApiKey, &QLineEdit::textEdited, this, &ProviderSettingsPage::changed);
    connect(m_openAiModel, &QComboBox::currentTextChanged, this, &ProviderSettingsPage::changed);
    connect(m_openAiEffort, &QComboBox::currentIndexChanged, this, &ProviderSettingsPage::changed);
    connect(m_anthropicModel, &QComboBox::currentTextChanged, this, [this] {
        updateAnthropicControls();
        emit changed();
    });
    connect(m_anthropicEffort, &QComboBox::currentIndexChanged, this, &ProviderSettingsPage::changed);
    connect(m_anthropicAuthMode, &QComboBox::currentIndexChanged, this, [this] {
        updateAnthropicAuthControl();
        emit changed();
    });
    connect(m_openAiCliproxyAccount, &QComboBox::currentIndexChanged, this, &ProviderSettingsPage::changed);
    connect(m_anthropicCliproxyAccount, &QComboBox::currentIndexChanged, this, &ProviderSettingsPage::changed);
    connect(m_speechAuthMode, &QComboBox::currentIndexChanged, this, &ProviderSettingsPage::changed);
    connect(anthropicInfoButton, &QPushButton::clicked, this, &ProviderSettingsPage::showAnthropicAuthInfo);
    connect(m_authMode, &QComboBox::currentIndexChanged, this, [this] {
        updateAuthControl();
        emit changed();
    });
    connect(m_apiKey, &QLineEdit::textEdited, this, [this] {
        ++m_apiKeyEditRevision;
    });
    connect(m_apiKey, &QLineEdit::textChanged, this, &ProviderSettingsPage::changed);
}

void ProviderSettingsPage::loadModels()
{
    settings::selectEditableText(m_openAiModel, m_settings.openAiModel());
    settings::selectData(m_openAiEffort, m_settings.openAiEffort());
    settings::selectEditableText(m_anthropicModel, m_settings.anthropicModel());
    settings::selectData(m_anthropicEffort, m_settings.anthropicEffort());
}

void ProviderSettingsPage::loadAuthModes()
{
    settings::selectData(m_authMode, m_settings.openAiAuthMode());
    settings::selectData(m_anthropicAuthMode, m_settings.anthropicAuthMode());
    settings::selectData(m_speechAuthMode, m_settings.speechAuthMode());
    m_cliproxyBaseUrl->setText(m_settings.cliproxyBaseUrl());
    m_cliproxyApiKey->setText(m_settings.cliproxyApiKey());
    updateCliproxyServerVisibility();
    populateCliproxyAccounts(m_openAiCliproxyAccount, QStringLiteral("codex"), m_settings.openAiCliproxyAccount());
    populateCliproxyAccounts(m_anthropicCliproxyAccount, QStringLiteral("claude"), m_settings.anthropicCliproxyAccount());
    const QString openAiMode = m_authMode->currentData().toString();
    m_authControl->setCurrentWidget(
        openAiMode == QStringLiteral("settings")      ? static_cast<QWidget *>(m_apiKey)
            : openAiMode == QStringLiteral("cliproxy") ? static_cast<QWidget *>(m_openAiCliproxyAccount)
                                                       : static_cast<QWidget *>(m_authStatus));
    m_anthropicCliproxyAccount->setVisible(m_anthropicAuthMode->currentData().toString() == QStringLiteral("cliproxy"));
    updateAnthropicControls();
}

void ProviderSettingsPage::loadSecret()
{
    const quint64 editRevision = m_apiKeyEditRevision;
    const QString apiKey = m_secrets.apiKey();
    m_secretLoaded = true;
    if (editRevision == m_apiKeyEditRevision) {
        const QSignalBlocker blocker(m_apiKey);
        m_loadedApiKey = apiKey;
        m_apiKey->setText(apiKey);
    }
    updateAuthControl();
}

void ProviderSettingsPage::appendToDraft(AppSettings &draft) const
{
    draft.refinement.openAiModel = settings::editableComboValue(m_openAiModel);
    draft.refinement.openAiEffort = m_openAiEffort->currentData().toString();
    draft.refinement.anthropicModel = settings::editableComboValue(m_anthropicModel);
    draft.refinement.anthropicEffort = m_anthropicEffort->currentData().toString();
}

void ProviderSettingsPage::saveAuthModes()
{
    m_settings.setOpenAiAuthMode(m_authMode->currentData().toString());
    m_settings.setAnthropicAuthMode(m_anthropicAuthMode->currentData().toString());
    m_settings.setSpeechAuthMode(m_speechAuthMode->currentData().toString());
    m_settings.setCliproxyBaseUrl(m_cliproxyBaseUrl->text());
    m_settings.setCliproxyApiKey(m_cliproxyApiKey->text());
    if (m_authMode->currentData().toString() == QStringLiteral("cliproxy")) {
        m_settings.setOpenAiCliproxyAccount(m_openAiCliproxyAccount->currentData().toString());
    }
    if (m_anthropicAuthMode->currentData().toString() == QStringLiteral("cliproxy")) {
        m_settings.setAnthropicCliproxyAccount(m_anthropicCliproxyAccount->currentData().toString());
    }
}

bool ProviderSettingsPage::saveSecret()
{
    if (m_settings.openAiAuthMode() == QStringLiteral("settings")
        && ((!m_secretLoaded && m_apiKeyEditRevision > 0)
            || (m_secretLoaded && m_apiKey->text().trimmed() != m_loadedApiKey))) {
        if (!m_secrets.saveApiKey(m_apiKey->text().trimmed())) {
            QMessageBox::warning(this,
                                 QStringLiteral("OpenAI key not saved"),
                                 m_secrets.status());
            return false;
        }
        m_loadedApiKey = m_apiKey->text().trimmed();
        m_secretLoaded = true;
    }
    if (m_secretLoaded || m_apiKeyEditRevision > 0) {
        updateAuthControl();
    }
    updateAnthropicControls();
    return true;
}

bool ProviderSettingsPage::hasModelChanges() const
{
    return settings::editableComboValue(m_openAiModel) != m_settings.openAiModel()
        || m_openAiEffort->currentData().toString() != m_settings.openAiEffort()
        || settings::editableComboValue(m_anthropicModel) != m_settings.anthropicModel()
        || m_anthropicEffort->currentData().toString() != m_settings.anthropicEffort();
}

bool ProviderSettingsPage::hasAuthChanges() const
{
    return m_authMode->currentData().toString() != m_settings.openAiAuthMode()
        || m_anthropicAuthMode->currentData().toString() != m_settings.anthropicAuthMode()
        || m_speechAuthMode->currentData().toString() != m_settings.speechAuthMode()
        || (m_authMode->currentData().toString() == QStringLiteral("cliproxy")
            && m_openAiCliproxyAccount->currentData().toString() != m_settings.openAiCliproxyAccount())
        || (m_anthropicAuthMode->currentData().toString() == QStringLiteral("cliproxy")
            && m_anthropicCliproxyAccount->currentData().toString() != m_settings.anthropicCliproxyAccount())
        || editedCliproxyBaseUrl() != m_settings.cliproxyBaseUrl()
        || m_cliproxyApiKey->text().trimmed() != m_settings.cliproxyApiKey()
        || (m_authMode->currentData().toString() == QStringLiteral("settings")
            && ((!m_secretLoaded && m_apiKeyEditRevision > 0)
                || (m_secretLoaded && m_apiKey->text().trimmed() != m_loadedApiKey)));
}

QString ProviderSettingsPage::editedCliproxyBaseUrl() const
{
    QString base = m_cliproxyBaseUrl->text().trimmed();
    while (base.endsWith(QLatin1Char('/'))) {
        base.chop(1);
    }
    return base;
}

void ProviderSettingsPage::updateCliproxyServerVisibility()
{
    const bool cliproxyInUse = m_authMode->currentData().toString() == QStringLiteral("cliproxy")
        || m_anthropicAuthMode->currentData().toString() == QStringLiteral("cliproxy");
    m_cliproxyCard->setVisible(cliproxyInUse);
}

void ProviderSettingsPage::updateAuthControl()
{
    const quint64 generation = ++m_authStatusGeneration;
    const QString mode = m_authMode->currentData().toString();
    if (mode == QStringLiteral("settings")) {
        m_authControl->setCurrentWidget(m_apiKey);
        m_apiKey->setPlaceholderText(m_secretLoaded
                                         ? m_secrets.status()
                                         : QStringLiteral("Loading app settings key…"));
        return;
    }
    if (mode == QStringLiteral("cliproxy")) {
        populateCliproxyAccounts(m_openAiCliproxyAccount,
                                 QStringLiteral("codex"),
                                 comboSelection(m_openAiCliproxyAccount, m_settings.openAiCliproxyAccount()));
        m_authControl->setCurrentWidget(m_openAiCliproxyAccount);
        return;
    }
    m_authControl->setCurrentWidget(m_authStatus);
    if (!m_secretLoaded) {
        m_authStatus->setText(QStringLiteral("Loading credentials…"));
        return;
    }
    m_authStatus->setText(QStringLiteral("Checking…"));

    const QString cliproxyAccount = m_settings.openAiCliproxyAccount();
    const QString cliproxyDir = m_settings.cliproxyOauthDir();
    const QString settingsApiKey = m_loadedApiKey;
    const QString settingsStatus = m_secrets.status();
    const QString cliproxyBaseUrl = m_settings.cliproxyBaseUrl();
    const QString cliproxyApiKey = m_settings.cliproxyApiKey();
    auto status = std::make_shared<QString>();
    QThread *thread = QThread::create([mode,
                                       cliproxyAccount,
                                       cliproxyDir,
                                       settingsApiKey,
                                       settingsStatus,
                                       cliproxyBaseUrl,
                                       cliproxyApiKey,
                                       status] {
        *status = OpenAiAuthProvider(nullptr,
                                     mode,
                                     cliproxyAccount,
                                     cliproxyDir,
                                     settingsApiKey,
                                     settingsStatus,
                                     cliproxyBaseUrl,
                                     cliproxyApiKey)
                      .status();
    });
    connect(thread, &QThread::finished, this, [this, generation, mode, status] {
        if (generation == m_authStatusGeneration
            && mode == m_authMode->currentData().toString()) {
            m_authStatus->setText(*status);
        }
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void ProviderSettingsPage::updateAnthropicAuthControl()
{
    const bool cliproxy = m_anthropicAuthMode->currentData().toString() == QStringLiteral("cliproxy");
    if (cliproxy) {
        populateCliproxyAccounts(m_anthropicCliproxyAccount,
                                 QStringLiteral("claude"),
                                 comboSelection(m_anthropicCliproxyAccount, m_settings.anthropicCliproxyAccount()));
    }
    m_anthropicCliproxyAccount->setVisible(cliproxy);
}

QString ProviderSettingsPage::comboSelection(const QComboBox *combo, const QString &stored)
{
    const QString current = combo->currentData().toString();
    return current.isEmpty() ? stored : current;
}

void ProviderSettingsPage::populateCliproxyAccounts(QComboBox *combo, const QString &type, const QString &selected)
{
    const QSignalBlocker blocker(combo);
    combo->clear();
    const QString baseUrl = editedCliproxyBaseUrl();
    if (!baseUrl.isEmpty()) {
        combo->addItem(QStringLiteral("Server-routed via %1").arg(baseUrl), QString());
        settings::setComboItemEnabled(combo, 0, false,
                                      QStringLiteral("CLI Proxy API picks the account (cliproxy/baseUrl is set)"));
        return;
    }
    const QString directory = m_settings.cliproxyOauthDir();
    const QList<CliProxyAccount> accounts = CliProxyCredentials::listAccounts(directory, type);
    // With several accounts and none chosen yet, force an explicit choice
    // instead of silently pinning whichever file sorts first.
    if (selected.isEmpty() && accounts.size() > 1) {
        combo->addItem(QStringLiteral("Choose an account…"), QString());
    }
    for (const CliProxyAccount &account : accounts) {
        combo->addItem(account.expired ? account.label + QStringLiteral(" (expired)") : account.label, account.fileName);
        if (account.disabled) {
            settings::setComboItemEnabled(combo, combo->count() - 1, false, QStringLiteral("Disabled in CLI Proxy API"));
        }
    }
    // Keep a stored selection visible even if its file is currently missing.
    if (!selected.isEmpty() && combo->findData(selected) < 0) {
        combo->addItem(selected + QStringLiteral(" (missing)"), selected);
    }
    if (combo->count() == 0) {
        combo->addItem(QStringLiteral("No accounts found"), QString());
        settings::setComboItemEnabled(combo, 0, false, directory);
    }
    settings::selectData(combo, selected);
}

void ProviderSettingsPage::updateAnthropicControls()
{
    const QString model = settings::editableComboValue(m_anthropicModel).toCaseFolded();
    const bool haiku = model.contains(QStringLiteral("haiku"));
    if (m_anthropicWarningRow) {
        m_anthropicWarningRow->setVisible(haiku);
    }
    m_anthropicWarning->setText(haiku
                                    ? QStringLiteral("Haiku may treat transcript as instructions.")
                                    : QString());
}

void ProviderSettingsPage::showAnthropicAuthInfo()
{
    QMessageBox::information(
        this,
        QStringLiteral("Anthropic auth"),
        QStringLiteral("Speecher reads the existing Claude Code OAuth session from ~/.claude/.credentials.json and calls the Anthropic Messages API directly. It does not start or control a Claude Code agent session.\n\nWith CLI Proxy API auth, Speecher instead reads the selected account from CLI Proxy API's auth directory (~/.local/share/cliproxy-api/oauth). CLI Proxy API keeps those tokens refreshed; Speecher never writes to them."));
}

} // namespace speecher
