#include "ui/SettingsDialog.h"

#include "app/ApplicationController.h"
#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#include "platform/PlatformIntegration.h"
#include "providers/OpenAiAuthProvider.h"
#include "providers/ProviderRegistry.h"
#include "ui/Theme.h"
#include "ui/settings/BindingsSettingsPage.h"
#include "ui/settings/CorrectionsSettingsPage.h"
#include "ui/settings/GeneralSettingsPage.h"
#include "ui/settings/OutputSettingsPage.h"
#include "ui/settings/RefinementSettingsPage.h"
#include "ui/settings/SettingsPageSupport.h"
#include "ui/settings/VocabularySettingsPage.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QtMath>

namespace speecher {

using namespace settings;
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

SettingsDialog::SettingsDialog(ApplicationController *controller, QWidget *parent)
    : QDialog(parent)
    , m_controller(controller)
    , m_audioDevice(new QComboBox(this))
    , m_captureMode(new QComboBox(this))
    , m_vadEnabled(new QCheckBox(this))
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
    , m_scroll(new QScrollArea(this))
    , m_preRollMs(new QSpinBox(this))
    , m_postRollMs(new QSpinBox(this))
    , m_readinessTimeoutMs(new QSpinBox(this))
    , m_vadThreshold(new QSpinBox(this))
{
    setWindowTitle(QStringLiteral("Speecher Settings"));
    resize(980, 780);
    setMinimumSize(820, 620);
    m_audioDevice->setMinimumContentsLength(28);
    m_audioDevice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_audioDevice->setToolTip(QStringLiteral("Microphone Speecher records from."));
    m_captureMode->addItem(QStringLiteral("On demand"), QStringLiteral("on_demand"));
    m_captureMode->addItem(QStringLiteral("Warm"), QStringLiteral("warm"));
    m_captureMode->setToolTip(QStringLiteral("Warm keeps the microphone stream open between captures for lower startup latency."));
    m_vadEnabled->setText(QStringLiteral("Trim silence"));
    m_vadEnabled->setToolTip(QStringLiteral("Suppress leading, trailing, and long in-between silence before audio is sent."));
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
    for (QSpinBox *spinBox : {m_preRollMs, m_postRollMs}) {
        spinBox->setRange(0, 1500);
        spinBox->setSingleStep(50);
        spinBox->setSuffix(QStringLiteral(" ms"));
    }
    m_readinessTimeoutMs->setRange(150, 3000);
    m_readinessTimeoutMs->setSingleStep(50);
    m_readinessTimeoutMs->setSuffix(QStringLiteral(" ms"));
    m_vadThreshold->setRange(1, 20);
    m_vadThreshold->setSuffix(QStringLiteral("%"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *audioSection = makeSectionLabel(QStringLiteral("Audio"), this);
    auto *openAiSection = makeSectionLabel(QStringLiteral("OpenAI"), this);
    auto *anthropicSection = makeSectionLabel(QStringLiteral("Anthropic"), this);
    auto *vocabularySection = makeSectionLabel(QStringLiteral("Vocabulary"), this);
    auto *correctionsSection = makeSectionLabel(QStringLiteral("Learned Corrections"), this);
    auto *bindingsSection = makeSectionLabel(QStringLiteral("Replacements & snippets"), this);

    auto *audioCard = makeSettingsCard(this);
    auto *audioLayout = qobject_cast<QVBoxLayout *>(audioCard->layout());
    auto *openAiCard = makeSettingsCard(this);
    auto *openAiLayout = qobject_cast<QVBoxLayout *>(openAiCard->layout());
    auto *anthropicCard = makeSettingsCard(this);
    auto *anthropicLayout = qobject_cast<QVBoxLayout *>(anthropicCard->layout());

    m_authStatus->setObjectName(QStringLiteral("statusText"));
    m_authStatus->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_authStatus->setWordWrap(false);
    m_authStatus->setMinimumWidth(170);
    m_authStatus->setAttribute(Qt::WA_StyledBackground, false);
    m_authStatus->setAutoFillBackground(false);
    m_anthropicWarning->setObjectName(QStringLiteral("statusText"));
    m_anthropicWarning->setForegroundRole(QPalette::WindowText);
    m_anthropicInfoButton = new QPushButton(this);
    m_anthropicInfoButton->setIcon(informationIcon(this));
    m_anthropicInfoButton->setIconSize(QSize(14, 14));
    m_anthropicInfoButton->setFlat(true);
    m_anthropicInfoButton->setCursor(Qt::PointingHandCursor);
    m_anthropicInfoButton->setFixedSize(22, 22);
    m_anthropicInfoButton->setToolTip(QStringLiteral("How Anthropic OAuth is used."));
    m_anthropicInfoButton->setAccessibleName(QStringLiteral("Anthropic auth info"));
    m_authStatus->setForegroundRole(QPalette::WindowText);

    addRow(audioLayout,
           makeRow(QStringLiteral("Microphone"),
                   QStringLiteral("Input device used for dictation."),
                   m_audioDevice,
                   audioCard),
           audioCard);
    addRow(audioLayout,
           makeRow(QStringLiteral("Capture mode"),
                   QStringLiteral("Open the microphone only while listening, or keep it warm between captures."),
                   m_captureMode,
                   audioCard),
           audioCard);
    addRow(audioLayout,
           makeRow(QStringLiteral("Pre-roll"),
                   QStringLiteral("Audio kept before speech or before a warm capture starts."),
                   m_preRollMs,
                   audioCard),
           audioCard);
    addRow(audioLayout,
           makeRow(QStringLiteral("Post-roll"),
                   QStringLiteral("Audio kept after stop or after speech falls quiet."),
                   m_postRollMs,
                   audioCard),
           audioCard);
    addRow(audioLayout,
           makeRow(QStringLiteral("Readiness timeout"),
                   QStringLiteral("How long Speecher waits for the first microphone sample."),
                   m_readinessTimeoutMs,
                   audioCard),
           audioCard);
    addRow(audioLayout,
           makeRow(QStringLiteral("Silence trimming"),
                   QStringLiteral("Optional VAD gate before sending audio to the speech provider."),
                   m_vadEnabled,
                   audioCard),
           audioCard);
    addRow(audioLayout,
           makeRow(QStringLiteral("Voice threshold"),
                   QStringLiteral("RMS level required before VAD treats audio as speech."),
                   m_vadThreshold,
                   audioCard),
           audioCard,
           false);

    addRow(openAiLayout, makeRow(QStringLiteral("OpenAI model"), QStringLiteral("Model used for refinement."), m_openAiModel, openAiCard), openAiCard);
    addRow(openAiLayout, makeRow(QStringLiteral("OpenAI effort"), QStringLiteral("Reasoning effort used for refinement."), m_openAiEffort, openAiCard), openAiCard);
    addRow(openAiLayout, makeRow(QStringLiteral("OpenAI auth mode"), QStringLiteral("Credential source used for refinement."), m_authMode, openAiCard), openAiCard);
    addRow(openAiLayout, makeRow(QStringLiteral("OpenAI auth"), QStringLiteral("Current credential source or app settings key."), m_authControl, openAiCard), openAiCard, false);

    addRow(anthropicLayout,
           makeRow(QStringLiteral("Claude model"),
                   QStringLiteral("Model used for Anthropic refinement."),
                   makeAnthropicModelControl(m_anthropicModel, m_anthropicWarning, &m_anthropicWarningRow, anthropicCard),
                   anthropicCard),
           anthropicCard);
    addRow(anthropicLayout,
           makeRow(QStringLiteral("Claude effort"),
                   QStringLiteral("Token spend and reasoning depth for Anthropic refinement."),
                   m_anthropicEffort,
                   anthropicCard),
           anthropicCard);
    addRow(anthropicLayout,
           makeRow(QStringLiteral("Anthropic auth"),
                   QStringLiteral("How Speecher sends refinement requests to Claude."),
                   m_anthropicAuthMode,
                   anthropicCard,
                   m_anthropicInfoButton),
           anthropicCard,
           false);

    m_vocabularyPage = new VocabularySettingsPage(this);
    m_correctionsPage = new CorrectionsSettingsPage(this);
    m_bindingsPage = new BindingsSettingsPage(m_scroll, this);

    auto *note = new QLabel(QStringLiteral("Automatic OpenAI auth follows the Codex auth mode when available, then falls back to Codex API key, Codex OAuth, OPENAI_API_KEY, and the app settings key. Codex OAuth uses the ChatGPT Codex backend. The app settings key is stored in the desktop keyring through QtKeychain when available."), this);
    note->setObjectName(QStringLiteral("noteText"));
    note->setWordWrap(true);
    note->setForegroundRole(QPalette::WindowText);
    note->setAttribute(Qt::WA_StyledBackground, false);

    m_generalPage = new GeneralSettingsPage(m_controller->primaryOutputStatus(), this);
    m_outputPage = new OutputSettingsPage(*m_controller->settings(), this);
    m_refinementPage = new RefinementSettingsPage(*m_controller->providerRegistry(), this);

    auto *audioPage = new QScrollArea(this);
    auto *audioPageLayout = makeSettingsPage(audioPage);
    audioPageLayout->addWidget(audioSection);
    audioPageLayout->addWidget(audioCard);
    audioPageLayout->addStretch();

    auto *providersPage = new QScrollArea(this);
    auto *providersPageLayout = makeSettingsPage(providersPage);
    providersPageLayout->addWidget(openAiSection);
    providersPageLayout->addWidget(openAiCard);
    providersPageLayout->addWidget(anthropicSection);
    providersPageLayout->addWidget(anthropicCard);
    providersPageLayout->addWidget(note);
    providersPageLayout->addStretch();

    auto *vocabularyPageLayout = makeSettingsPage(m_scroll);
    vocabularyPageLayout->addWidget(vocabularySection);
    vocabularyPageLayout->addWidget(m_vocabularyPage);
    vocabularyPageLayout->addWidget(correctionsSection);
    vocabularyPageLayout->addWidget(m_correctionsPage);
    vocabularyPageLayout->addWidget(bindingsSection);
    vocabularyPageLayout->addWidget(m_bindingsPage);
    vocabularyPageLayout->addStretch();

    const QList<QPair<QString, QString>> categories{
        {QStringLiteral("General"), QStringLiteral("preferences-system")},
        {QStringLiteral("Audio"), QStringLiteral("audio-input-microphone")},
        {QStringLiteral("Output"), QStringLiteral("edit-paste")},
        {QStringLiteral("Refinement"), QStringLiteral("document-edit")},
        {QStringLiteral("Providers"), QStringLiteral("network-server")},
        {QStringLiteral("Vocabulary"), QStringLiteral("tools-check-spelling")},
    };
    const QList<QWidget *> pages{
        m_generalPage,
        audioPage,
        m_outputPage,
        m_refinementPage,
        providersPage,
        m_scroll,
    };

    auto *body = new QWidget(this);
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    addPageContainer(bodyLayout,
                     categories,
                     pages,
                     &m_categories,
                     &m_pages,
                     body);
    root->addWidget(body, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    buttons->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    auto *footer = new QFrame(this);
    footer->setFrameShape(QFrame::NoFrame);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(16, 12, 16, 12);
    m_runtimeStatus = new QLabel(
        QStringLiteral("Dictation: %1").arg(m_controller->stateName()),
        footer);
    footerLayout->addWidget(m_runtimeStatus);
    footerLayout->addStretch();
    footerLayout->addWidget(buttons);
    root->addWidget(footer);

    for (QLabel *label : findChildren<QLabel *>()) {
        if (label->objectName() == QStringLiteral("subsectionLabel")) {
            QFont font = label->font();
            font.setBold(true);
            label->setFont(font);
        } else if (label->objectName() == QStringLiteral("rowDescription")
                   || label->objectName() == QStringLiteral("noteText")) {
            label->setForegroundRole(QPalette::PlaceholderText);
        }
    }

    if (QPushButton *ok = buttons->button(QDialogButtonBox::Ok)) {
        m_okButton = ok;
        ok->setDefault(true);
        ok->setAutoDefault(true);
        ok->setIcon(QIcon());
    }
    if (QPushButton *cancel = buttons->button(QDialogButtonBox::Cancel)) {
        cancel->setAutoDefault(false);
        cancel->setIcon(QIcon());
    }
    if (QPushButton *apply = buttons->button(QDialogButtonBox::Apply)) {
        m_applyButton = apply;
        apply->setAutoDefault(false);
        apply->setIcon(QIcon());
    }

    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (save()) {
            accept();
        }
    });
    connect(m_applyButton, &QPushButton::clicked, this, [this] {
        save();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_controller,
            &ApplicationController::statusChanged,
            m_runtimeStatus,
            [this](const QString &status) {
                m_runtimeStatus->setText(QStringLiteral("Dictation: %1").arg(status));
            });
    connect(m_generalPage, &GeneralSettingsPage::changed, this, &SettingsDialog::updateButtonState);
    connect(m_audioDevice, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateButtonState);
    connect(m_captureMode, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateButtonState);
    connect(m_vadEnabled, &QCheckBox::toggled, this, [this] {
        updateAudioControls();
        updateButtonState();
    });
    connect(m_preRollMs, &QSpinBox::valueChanged, this, &SettingsDialog::updateButtonState);
    connect(m_postRollMs, &QSpinBox::valueChanged, this, &SettingsDialog::updateButtonState);
    connect(m_readinessTimeoutMs, &QSpinBox::valueChanged, this, &SettingsDialog::updateButtonState);
    connect(m_vadThreshold, &QSpinBox::valueChanged, this, &SettingsDialog::updateButtonState);
    connect(m_openAiModel, &QComboBox::currentTextChanged, this, &SettingsDialog::updateButtonState);
    connect(m_openAiEffort, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateButtonState);
    connect(m_anthropicModel, &QComboBox::currentTextChanged, this, [this] {
        updateAnthropicControls();
        updateButtonState();
    });
    connect(m_anthropicEffort, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateButtonState);
    connect(m_anthropicAuthMode, &QComboBox::currentIndexChanged, this, [this] {
        updateButtonState();
    });
    connect(m_anthropicInfoButton, &QPushButton::clicked, this, &SettingsDialog::showAnthropicAuthInfo);
    connect(m_correctionsPage, &CorrectionsSettingsPage::changed, this, &SettingsDialog::updateButtonState);
    connect(m_authMode, &QComboBox::currentIndexChanged, this, [this] {
        updateAuthControl();
        updateButtonState();
    });
    connect(m_apiKey, &QLineEdit::textChanged, this, &SettingsDialog::updateButtonState);
    connect(m_outputPage, &OutputSettingsPage::changed, this, &SettingsDialog::updateButtonState);
    connect(m_refinementPage, &RefinementSettingsPage::changed, this, &SettingsDialog::updateButtonState);
    connect(m_vocabularyPage, &VocabularySettingsPage::changed, this, &SettingsDialog::updateButtonState);
    connect(m_bindingsPage, &BindingsSettingsPage::changed, this, &SettingsDialog::updateButtonState);
    load();
}

void SettingsDialog::load()
{
    SettingsStore *settings = m_controller->settings();
    m_generalPage->load(settings->snapshot());
    const AudioCaptureSettings audio = settings->audioCaptureSettings();
    refreshAudioDeviceList(audio.deviceId);
    selectData(m_captureMode, audio.mode);
    m_vadEnabled->setChecked(audio.vadEnabled);
    m_preRollMs->setValue(audio.preRollMs);
    m_postRollMs->setValue(audio.postRollMs);
    m_readinessTimeoutMs->setValue(audio.readinessTimeoutMs);
    m_vadThreshold->setValue(audio.vadThresholdPercent);
    m_refinementPage->load(settings->snapshot());
    selectEditableText(m_openAiModel, settings->openAiModel());
    selectData(m_openAiEffort, settings->openAiEffort());
    selectEditableText(m_anthropicModel, settings->anthropicModel());
    selectData(m_anthropicEffort, settings->anthropicEffort());
    m_outputPage->load(settings->snapshot());
    selectData(m_authMode, settings->openAiAuthMode());
    selectData(m_anthropicAuthMode, settings->anthropicAuthMode());
    m_apiKey->setText(m_controller->secretStore()->apiKey());
    m_vocabularyPage->load(settings->vocabularyEntries());
    m_bindingsPage->load(settings->bindingRules());
    m_correctionsPage->load(settings->correctionLearningEnabled(), settings->learnedCorrections());
    updateAudioControls();
    updateAuthControl();
    updateAnthropicControls();
    m_outputPage->refreshControls();
    updateButtonState();
}

bool SettingsDialog::save()
{
    SettingsStore *settings = m_controller->settings();
    QList<BindingRule> bindingRules;
    if (!m_bindingsPage->validate(&bindingRules)) {
        return false;
    }
    if (!m_outputPage->validate()) {
        return false;
    }
    if (!m_refinementPage->validate()) {
        return false;
    }

    AppSettings draft = settings->snapshot();
    m_generalPage->appendToDraft(draft);
    m_outputPage->appendToDraft(draft);
    m_refinementPage->appendToDraft(draft);
    settings->setTheme(draft.ui.theme);
    Theme::apply(settings->theme());
    settings->setPauseMediaDuringTranscription(draft.ui.pauseMediaDuringTranscription);
    settings->setSoundsEnabled(draft.ui.soundsEnabled);
    settings->setPreviewWords(draft.ui.previewWords);
    settings->setAudioCaptureSettings({
        m_audioDevice->currentData().toString(),
        m_captureMode->currentData().toString(),
        m_vadEnabled->isChecked(),
        m_preRollMs->value(),
        m_postRollMs->value(),
        m_readinessTimeoutMs->value(),
        m_vadThreshold->value(),
    });
    settings->setRefinementProvider(draft.refinement.providerId);
    settings->setDefaultWritingProfile(draft.refinement.defaultWritingProfile);
    settings->setWritingProfileSettings(draft.refinement.writingProfiles);
    settings->setWritingProfileOverrides(draft.refinement.writingProfileOverrides);
    settings->setUseTargetContext(draft.refinement.useTargetContext);
    settings->setIncludeScreenshotContext(draft.refinement.includeScreenshotContext);
    settings->setOpenAiModel(editableComboValue(m_openAiModel));
    selectEditableText(m_openAiModel, settings->openAiModel());
    settings->setOpenAiEffort(m_openAiEffort->currentData().toString());
    settings->setAnthropicModel(editableComboValue(m_anthropicModel));
    selectEditableText(m_anthropicModel, settings->anthropicModel());
    settings->setAnthropicEffort(m_anthropicEffort->currentData().toString());
    settings->setOutputMethod(draft.output.method);
    settings->setOutputFormat(draft.output.format);
    settings->setPasteRules(draft.output.pasteRules);
    settings->setRestoreClipboardAfterTyping(draft.output.restoreClipboardAfterTyping);
    settings->setOpenAiAuthMode(m_authMode->currentData().toString());
    settings->setAnthropicAuthMode(m_anthropicAuthMode->currentData().toString());
    settings->setVocabularyEntries(m_vocabularyPage->entries());
    settings->setCorrectionLearningEnabled(m_correctionsPage->learningEnabled());
    settings->setLearnedCorrections(m_correctionsPage->corrections());
    settings->setBindingRules(bindingRules);
    m_vocabularyPage->load(settings->vocabularyEntries());
    m_bindingsPage->load(settings->bindingRules());
    m_correctionsPage->load(settings->correctionLearningEnabled(), settings->learnedCorrections());
    if (settings->openAiAuthMode() == QStringLiteral("settings")) {
        if (!m_controller->secretStore()->saveApiKey(m_apiKey->text().trimmed())) {
            QMessageBox::warning(this,
                                 QStringLiteral("OpenAI key not saved"),
                                 m_controller->secretStore()->status());
            return false;
        }
    }
    updateAuthControl();
    updateAnthropicControls();
    m_outputPage->refreshControls();
    updateButtonState();
    return true;
}

bool SettingsDialog::hasChanges() const
{
    const SettingsStore *settings = m_controller->settings();
    const AudioCaptureSettings audio = settings->audioCaptureSettings();
    if (m_generalPage->hasChanges(settings->snapshot())
        || m_audioDevice->currentData().toString() != audio.deviceId
        || m_captureMode->currentData().toString() != audio.mode
        || m_vadEnabled->isChecked() != audio.vadEnabled
        || m_preRollMs->value() != audio.preRollMs
        || m_postRollMs->value() != audio.postRollMs
        || m_readinessTimeoutMs->value() != audio.readinessTimeoutMs
        || m_vadThreshold->value() != audio.vadThresholdPercent
        || m_refinementPage->hasChanges(settings->snapshot())
        || editableComboValue(m_openAiModel) != settings->openAiModel()
        || m_openAiEffort->currentData().toString() != settings->openAiEffort()
        || editableComboValue(m_anthropicModel) != settings->anthropicModel()
        || m_anthropicEffort->currentData().toString() != settings->anthropicEffort()
        || m_outputPage->hasChanges(settings->snapshot())
        || m_authMode->currentData().toString() != settings->openAiAuthMode()
        || m_anthropicAuthMode->currentData().toString() != settings->anthropicAuthMode()
        || m_vocabularyPage->hasChanges(settings->vocabularyEntries())
        || m_correctionsPage->hasChanges(settings->correctionLearningEnabled(), settings->learnedCorrections())
        || m_bindingsPage->hasChanges(settings->bindingRules())) {
        return true;
    }

    return m_authMode->currentData().toString() == QStringLiteral("settings")
        && m_apiKey->text().trimmed() != m_controller->secretStore()->apiKey();
}

void SettingsDialog::refreshAudioDeviceList(const QString &selectedDeviceId)
{
    const QSignalBlocker blocker(m_audioDevice);
    m_audioDevice->clear();

    const QList<AudioInputDeviceInfo> devices = m_controller->platform()->availableAudioInputDevices();
    if (devices.isEmpty()) {
        m_audioDevice->addItem(QStringLiteral("No microphones found"), QString());
        setComboItemEnabled(m_audioDevice,
                            0,
                            false,
                            QStringLiteral("Connect or enable an input device, then reopen Settings."));
        if (!selectedDeviceId.isEmpty()) {
            m_audioDevice->addItem(QStringLiteral("Missing microphone"), selectedDeviceId);
            setComboItemEnabled(m_audioDevice,
                                1,
                                false,
                                QStringLiteral("This saved microphone is not currently available."));
            selectData(m_audioDevice, selectedDeviceId);
        }
        return;
    }

    m_audioDevice->addItem(QStringLiteral("System default"), QString());
    bool selectedFound = selectedDeviceId.isEmpty();
    for (const AudioInputDeviceInfo &device : devices) {
        const QString label = device.isDefault
            ? QStringLiteral("%1 (default)").arg(device.label)
            : device.label;
        m_audioDevice->addItem(label, device.id);
        selectedFound = selectedFound || device.id == selectedDeviceId;
    }

    if (!selectedFound) {
        m_audioDevice->addItem(QStringLiteral("Missing microphone"), selectedDeviceId);
        const int missingIndex = m_audioDevice->count() - 1;
        setComboItemEnabled(m_audioDevice,
                            missingIndex,
                            false,
                            QStringLiteral("This saved microphone is not currently available."));
    }

    selectData(m_audioDevice, selectedDeviceId);
}

void SettingsDialog::updateAudioControls()
{
    m_vadThreshold->setEnabled(m_vadEnabled->isChecked());
}

void SettingsDialog::updateAuthControl()
{
    const QString mode = m_authMode->currentData().toString();
    if (mode == QStringLiteral("settings")) {
        m_authControl->setCurrentWidget(m_apiKey);
        m_apiKey->setPlaceholderText(m_controller->secretStore()->status());
        return;
    }
    m_authStatus->setText(OpenAiAuthProvider(m_controller->secretStore(), mode).status());
    m_authControl->setCurrentWidget(m_authStatus);
}

void SettingsDialog::updateAnthropicControls()
{
    const QString model = editableComboValue(m_anthropicModel).toCaseFolded();
    const bool haiku = model.contains(QStringLiteral("haiku"));
    if (m_anthropicWarningRow) {
        m_anthropicWarningRow->setVisible(haiku);
    }
    m_anthropicWarning->setText(haiku
                                    ? QStringLiteral("Haiku may treat transcript as instructions.")
                                    : QString());
}

void SettingsDialog::showAnthropicAuthInfo()
{
    QMessageBox::information(
        this,
        QStringLiteral("Anthropic auth"),
        QStringLiteral("Speecher reads the existing Claude Code OAuth session from ~/.claude/.credentials.json and calls the Anthropic Messages API directly. It does not start or control a Claude Code agent session."));
}

void SettingsDialog::updateButtonState()
{
    const bool changed = hasChanges();
    if (m_okButton) {
        m_okButton->setEnabled(changed);
    }
    if (m_applyButton) {
        m_applyButton->setEnabled(changed);
    }
}

} // namespace speecher
