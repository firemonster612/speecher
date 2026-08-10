#include "ui/settings/ProviderSettingsPage.h"

#include "core/SecretStore.h"
#include "core/SettingsStore.h"
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
#include <QVBoxLayout>
#include <QtMath>

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
    m_anthropicAuthMode->addItem(QStringLiteral("Claude OAuth"), QStringLiteral("oauth"));
    m_anthropicAuthMode->setToolTip(QStringLiteral("Use the existing Claude Code OAuth session for direct Anthropic API routing."));
    m_apiKey->setEchoMode(QLineEdit::Password);
    m_apiKey->setPlaceholderText(QStringLiteral("Enter OpenAI API key"));
    m_authControl->addWidget(m_authStatus);
    m_authControl->addWidget(m_apiKey);

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
    anthropicInfoButton->setToolTip(QStringLiteral("How Anthropic OAuth is used."));
    anthropicInfoButton->setAccessibleName(QStringLiteral("Anthropic auth info"));
    m_authStatus->setForegroundRole(QPalette::WindowText);

    auto *title = settings::makePageTitle(QStringLiteral("Providers"), this);
    auto *openAiSection = settings::makeSectionLabel(QStringLiteral("OpenAI"), this);
    auto *anthropicSection = settings::makeSectionLabel(QStringLiteral("Anthropic"), this);
    auto *openAiCard = settings::makeSettingsCard(this);
    auto *openAiLayout = qobject_cast<QFormLayout *>(openAiCard->layout());
    auto *anthropicCard = settings::makeSettingsCard(this);
    auto *anthropicLayout = qobject_cast<QFormLayout *>(anthropicCard->layout());

    settings::addRow(openAiLayout, settings::makeRow(QStringLiteral("OpenAI model"), QStringLiteral("Model used for refinement."), m_openAiModel, openAiCard), openAiCard);
    settings::addRow(openAiLayout, settings::makeRow(QStringLiteral("OpenAI effort"), QStringLiteral("Reasoning effort used for refinement."), m_openAiEffort, openAiCard), openAiCard);
    settings::addRow(openAiLayout, settings::makeRow(QStringLiteral("OpenAI auth mode"), QStringLiteral("Credential source used for refinement."), m_authMode, openAiCard), openAiCard);
    settings::addRow(openAiLayout, settings::makeRow(QStringLiteral("OpenAI auth"), QStringLiteral("Current credential source or app settings key."), m_authControl, openAiCard), openAiCard, false);

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
    settings::addRow(anthropicLayout,
                     settings::makeRow(QStringLiteral("Anthropic auth"),
                                       QStringLiteral("How Speecher sends refinement requests to Claude."),
                                       m_anthropicAuthMode,
                                       anthropicCard,
                                       anthropicInfoButton),
                     anthropicCard,
                     false);

    auto *note = new QLabel(QStringLiteral("Automatic OpenAI auth follows the Codex auth mode when available, then falls back to Codex API key, Codex OAuth, OPENAI_API_KEY, and the app settings key. Codex OAuth uses the ChatGPT Codex backend. The app settings key is stored in the desktop keyring through QtKeychain when available."), this);
    note->setObjectName(QStringLiteral("noteText"));
    note->setWordWrap(true);
    note->setForegroundRole(QPalette::WindowText);
    note->setAttribute(Qt::WA_StyledBackground, false);

    auto *pageLayout = settings::makeSettingsPage(this);
    pageLayout->setSpacing(0);
    pageLayout->addWidget(title);
    pageLayout->addSpacing(settings::sectionGap());
    pageLayout->addWidget(openAiSection);
    pageLayout->addSpacing(settings::tightSpacing());
    pageLayout->addWidget(openAiCard);
    pageLayout->addSpacing(settings::groupGap());
    pageLayout->addWidget(settings::makeCenteredSeparator(this));
    pageLayout->addSpacing(settings::groupGap());
    pageLayout->addWidget(anthropicSection);
    pageLayout->addSpacing(settings::tightSpacing());
    pageLayout->addWidget(anthropicCard);
    pageLayout->addSpacing(settings::relatedSpacing());
    pageLayout->addWidget(note);
    pageLayout->addStretch();

    connect(m_openAiModel, &QComboBox::currentTextChanged, this, &ProviderSettingsPage::changed);
    connect(m_openAiEffort, &QComboBox::currentIndexChanged, this, &ProviderSettingsPage::changed);
    connect(m_anthropicModel, &QComboBox::currentTextChanged, this, [this] {
        updateAnthropicControls();
        emit changed();
    });
    connect(m_anthropicEffort, &QComboBox::currentIndexChanged, this, &ProviderSettingsPage::changed);
    connect(m_anthropicAuthMode, &QComboBox::currentIndexChanged, this, &ProviderSettingsPage::changed);
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
    m_authControl->setCurrentWidget(
        m_authMode->currentData().toString() == QStringLiteral("settings")
            ? static_cast<QWidget *>(m_apiKey)
            : static_cast<QWidget *>(m_authStatus));
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
}

bool ProviderSettingsPage::saveSecret()
{
    if (m_settings.openAiAuthMode() == QStringLiteral("settings")
        && (m_secretLoaded || m_apiKeyEditRevision > 0)) {
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
        || (m_authMode->currentData().toString() == QStringLiteral("settings")
            && ((!m_secretLoaded && m_apiKeyEditRevision > 0)
                || (m_secretLoaded && m_apiKey->text().trimmed() != m_loadedApiKey)));
}

void ProviderSettingsPage::updateAuthControl()
{
    const QString mode = m_authMode->currentData().toString();
    if (mode == QStringLiteral("settings")) {
        m_authControl->setCurrentWidget(m_apiKey);
        m_apiKey->setPlaceholderText(m_secretLoaded
                                         ? m_secrets.status()
                                         : QStringLiteral("Loading app settings key…"));
        return;
    }
    m_authStatus->setText(OpenAiAuthProvider(&m_secrets, mode).status());
    m_authControl->setCurrentWidget(m_authStatus);
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
        QStringLiteral("Speecher reads the existing Claude Code OAuth session from ~/.claude/.credentials.json and calls the Anthropic Messages API directly. It does not start or control a Claude Code agent session."));
}

} // namespace speecher
