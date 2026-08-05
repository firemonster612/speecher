#include "ui/SettingsDialog.h"

#include "app/ApplicationController.h"
#include "core/BindingProcessor.h"
#include "core/OutputMethod.h"
#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#include "output/YdotoolDelivery.h"
#include "output/YdotoolSetup.h"
#include "platform/PlatformIntegration.h"
#include "providers/OpenAiAuthProvider.h"
#include "providers/ProviderRegistry.h"
#include "ui/Theme.h"
#include "ui/settings/GeneralSettingsPage.h"
#include "ui/settings/SettingsPageSupport.h"
#include "ui/settings/VocabularySettingsPage.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHash>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPainter>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>
#include <QUrl>
#include <QtMath>

#include <utility>

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

class ElidedLabel : public QLabel {
public:
    explicit ElidedLabel(QWidget *parent = nullptr)
        : QLabel(parent)
    {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setFont(font());
        painter.setPen(palette().color(foregroundRole()));
        painter.drawText(rect(), alignment(), fontMetrics().elidedText(text(), Qt::ElideRight, width()));
    }
};

static QString bindingPreview(const QString &replacement)
{
    QString preview = replacement;
    preview.replace(QLatin1Char('\n'), QStringLiteral(" / "));
    return preview.simplified();
}

static PasteMethod pasteMethodFor(const QList<PasteRule> &rules,
                                  PasteRuleScope scope,
                                  const QString &match,
                                  PasteMethod fallback)
{
    for (const PasteRule &rule : rules) {
        if (rule.scope == scope && rule.match == match) {
            return rule.method;
        }
    }
    return fallback;
}

static void addPasteMethods(QComboBox *combo,
                            bool includeDirectInsert = false,
                            bool includeGlobalFallback = false)
{
    if (includeGlobalFallback) {
        combo->addItem(QStringLiteral("Use global fallback"), QStringLiteral("inherit"));
    }
    combo->addItem(QStringLiteral("Standard paste (Ctrl+V)"), pasteMethodName(PasteMethod::StandardPaste));
    combo->addItem(QStringLiteral("Terminal paste (Ctrl+Shift+V)"), pasteMethodName(PasteMethod::TerminalPaste));
    if (includeDirectInsert) {
        combo->addItem(QStringLiteral("Direct insertion (AT-SPI)"), pasteMethodName(PasteMethod::DirectInsert));
    }
    combo->addItem(QStringLiteral("Clipboard only"), pasteMethodName(PasteMethod::ClipboardOnly));
}

static QList<AppCategory> managedPasteCategories()
{
    return {
        AppCategory::Terminal,
        AppCategory::Browser,
        AppCategory::Email,
        AppCategory::Office,
        AppCategory::CodeEditor,
        AppCategory::General,
    };
}

static void addCleanupStrengths(QComboBox *combo)
{
    combo->addItem(QStringLiteral("None"), QStringLiteral("none"));
    combo->addItem(QStringLiteral("Light"), QStringLiteral("light_cleanup"));
    combo->addItem(QStringLiteral("Medium"), QStringLiteral("balanced"));
    combo->addItem(QStringLiteral("High"), QStringLiteral("strong_polish"));
}

static void addWritingTones(QComboBox *combo)
{
    combo->addItem(QStringLiteral("No tone override"), QStringLiteral("none"));
    combo->addItem(QStringLiteral("Formal"), QStringLiteral("formal"));
    combo->addItem(QStringLiteral("Casual"), QStringLiteral("casual"));
    combo->addItem(QStringLiteral("Very casual"), QStringLiteral("very_casual"));
    combo->addItem(QStringLiteral("Excited"), QStringLiteral("excited"));
    combo->addItem(QStringLiteral("Gen Z"), QStringLiteral("gen_z"));
}

static void addWritingProfiles(QComboBox *combo)
{
    combo->addItem(QStringLiteral("Work"), QStringLiteral("work"));
    combo->addItem(QStringLiteral("Email"), QStringLiteral("email"));
    combo->addItem(QStringLiteral("Personal"), QStringLiteral("personal"));
    combo->addItem(QStringLiteral("Other"), QStringLiteral("other"));
}

static QString writingProfileLabel(WritingProfile profile)
{
    switch (profile) {
    case WritingProfile::Work:
        return QStringLiteral("Work");
    case WritingProfile::Email:
        return QStringLiteral("Email");
    case WritingProfile::Personal:
        return QStringLiteral("Personal");
    case WritingProfile::Other:
        return QStringLiteral("Other");
    }
    return QStringLiteral("Other");
}

static QList<PasteRule> withPasteRules(const QList<PasteRule> &existing,
                                      const QList<PasteRule> &applicationRules,
                                      const QList<PasteRule> &categoryRules,
                                      PasteMethod globalMethod)
{
    QList<PasteRule> rules = applicationRules;
    QSet<QString> managedCategories;
    for (AppCategory category : managedPasteCategories()) {
        managedCategories.insert(appCategoryName(category));
    }
    for (const PasteRule &rule : existing) {
        if (rule.scope == PasteRuleScope::Category
            && !managedCategories.contains(rule.match)) {
            rules.append(rule);
        }
    }
    rules.append(categoryRules);
    rules.append({PasteRuleScope::Global, QString(), globalMethod, true});
    return rules;
}

static QWidget *makeYdotoolControl(QLabel *status,
                                   QPushButton *setup,
                                   QPushButton *start,
                                   QPushButton *disable,
                                   QPushButton *remove,
                                   QWidget *parent)
{
    auto *control = new QWidget(parent);
    auto *layout = new QVBoxLayout(control);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    status->setObjectName(QStringLiteral("statusText"));
    status->setWordWrap(true);
    status->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    status->setForegroundRole(QPalette::WindowText);
    status->setAttribute(Qt::WA_StyledBackground, false);

    auto *row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    row->addWidget(setup);
    row->addWidget(start);
    row->addWidget(disable);
    row->addWidget(remove);

    layout->addWidget(status);
    layout->addLayout(row);
    return control;
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
    , m_provider(new QComboBox(this))
    , m_writingProfile(new QComboBox(this))
    , m_useTargetContext(new QCheckBox(this))
    , m_screenshotContext(new QCheckBox(this))
    , m_openAiModel(new QComboBox(this))
    , m_openAiEffort(new QComboBox(this))
    , m_anthropicModel(new QComboBox(this))
    , m_anthropicEffort(new QComboBox(this))
    , m_outputMethod(new QComboBox(this))
    , m_outputFormat(new QComboBox(this))
    , m_globalPaste(new QComboBox(this))
    , m_restoreClipboardAfterTyping(new QCheckBox(this))
    , m_learnCorrections(new QCheckBox(this))
    , m_authMode(new QComboBox(this))
    , m_anthropicAuthMode(new QComboBox(this))
    , m_authControl(new QStackedWidget(this))
    , m_authStatus(new QLabel(this))
    , m_anthropicWarning(new QLabel(this))
    , m_ydotoolStatus(new QLabel(this))
    , m_apiKey(new QLineEdit(this))
    , m_scroll(new QScrollArea(this))
    , m_preRollMs(new QSpinBox(this))
    , m_postRollMs(new QSpinBox(this))
    , m_readinessTimeoutMs(new QSpinBox(this))
    , m_vadThreshold(new QSpinBox(this))
    , m_corrections(new QTableWidget(this))
    , m_appPasteRules(new QTableWidget(this))
    , m_profileSettings(new QTableWidget(this))
    , m_appProfileOverrides(new QTableWidget(this))
    , m_bindings(new QListWidget(this))
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
    for (const ProviderDescriptor &provider : m_controller->providerRegistry()->refinementProviders()) {
        m_provider->addItem(provider.label, provider.id);
    }
    m_provider->addItem(QStringLiteral("None"), QStringLiteral("none"));
    addWritingProfiles(m_writingProfile);
    m_useTargetContext->setText(QStringLiteral("Use context"));
    m_screenshotContext->setText(QStringLiteral("Allow screenshots"));
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
    m_outputMethod->addItem(OutputMethod::label(QString::fromLatin1(OutputMethod::Automatic)), QString::fromLatin1(OutputMethod::Automatic));
    m_outputMethod->addItem(OutputMethod::label(QString::fromLatin1(OutputMethod::Ydotool)), QString::fromLatin1(OutputMethod::Ydotool));
    m_outputMethod->addItem(OutputMethod::label(QString::fromLatin1(OutputMethod::WlCopy)), QString::fromLatin1(OutputMethod::WlCopy));
    m_outputMethod->addItem(OutputMethod::label(QString::fromLatin1(OutputMethod::QtClipboard)), QString::fromLatin1(OutputMethod::QtClipboard));
    m_outputMethod->setToolTip(QStringLiteral("How Speecher delivers final text."));
    m_outputMethod->view()->setMouseTracking(true);
    m_outputFormat->addItem(QStringLiteral("Plain text"), QStringLiteral("plain"));
    m_outputFormat->addItem(QStringLiteral("HTML and plain text"), QStringLiteral("html"));
    addPasteMethods(m_globalPaste);
    for (AppCategory category : managedPasteCategories()) {
        auto *combo = new QComboBox(this);
        addPasteMethods(combo, false, true);
        m_categoryPasteControls.append({category, combo});
    }
    m_restoreClipboardAfterTyping->setText(QStringLiteral("Restore"));
    m_restoreClipboardAfterTyping->setToolTip(QStringLiteral("Restore the previous clipboard after virtual-keyboard paste."));
    m_learnCorrections->setText(QStringLiteral("Learn corrections"));
    m_learnCorrections->setToolTip(QStringLiteral("Observe a verified inserted span briefly and save only high-confidence corrections."));
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
    m_corrections->setObjectName(QStringLiteral("vocabInput"));
    m_corrections->setColumnCount(4);
    m_corrections->setHorizontalHeaderLabels({
        QStringLiteral("Enabled"),
        QStringLiteral("Heard"),
        QStringLiteral("Corrected"),
        QStringLiteral("App"),
    });
    m_corrections->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_corrections->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_corrections->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_corrections->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_corrections->verticalHeader()->hide();
    m_corrections->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_corrections->setSelectionMode(QAbstractItemView::SingleSelection);
    m_corrections->setMinimumHeight(180);
    m_appPasteRules->setObjectName(QStringLiteral("vocabInput"));
    m_appPasteRules->setColumnCount(3);
    m_appPasteRules->setHorizontalHeaderLabels({
        QStringLiteral("Enabled"),
        QStringLiteral("Application ID"),
        QStringLiteral("Paste behavior"),
    });
    m_appPasteRules->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_appPasteRules->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_appPasteRules->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_appPasteRules->verticalHeader()->hide();
    m_appPasteRules->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_appPasteRules->setSelectionMode(QAbstractItemView::SingleSelection);
    m_appPasteRules->setMinimumHeight(150);
    m_profileSettings->setObjectName(QStringLiteral("vocabInput"));
    m_profileSettings->setColumnCount(3);
    m_profileSettings->setHorizontalHeaderLabels({
        QStringLiteral("Profile"),
        QStringLiteral("Cleanup"),
        QStringLiteral("Tone"),
    });
    m_profileSettings->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_profileSettings->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_profileSettings->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_profileSettings->verticalHeader()->hide();
    m_profileSettings->setSelectionMode(QAbstractItemView::NoSelection);
    m_profileSettings->setMinimumHeight(172);
    m_profileSettings->setMaximumHeight(172);
    m_appProfileOverrides->setObjectName(QStringLiteral("vocabInput"));
    m_appProfileOverrides->setColumnCount(3);
    m_appProfileOverrides->setHorizontalHeaderLabels({
        QStringLiteral("Enabled"),
        QStringLiteral("Application ID"),
        QStringLiteral("Profile"),
    });
    m_appProfileOverrides->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_appProfileOverrides->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_appProfileOverrides->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_appProfileOverrides->verticalHeader()->hide();
    m_appProfileOverrides->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_appProfileOverrides->setSelectionMode(QAbstractItemView::SingleSelection);
    m_appProfileOverrides->setMinimumHeight(150);
    m_bindings->setObjectName(QStringLiteral("bindingList"));
    m_bindings->setUniformItemSizes(false);
    m_bindings->setAlternatingRowColors(false);
    m_bindings->setSelectionMode(QAbstractItemView::SingleSelection);
    m_bindings->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_bindings->setMinimumHeight(180);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *audioSection = makeSectionLabel(QStringLiteral("Audio"), this);
    auto *refinementSection = makeSectionLabel(QStringLiteral("Refinement"), this);
    auto *outputSection = makeSectionLabel(QStringLiteral("Output"), this);
    auto *openAiSection = makeSectionLabel(QStringLiteral("OpenAI"), this);
    auto *anthropicSection = makeSectionLabel(QStringLiteral("Anthropic"), this);
    auto *vocabularySection = makeSectionLabel(QStringLiteral("Vocabulary"), this);
    auto *correctionsSection = makeSectionLabel(QStringLiteral("Learned Corrections"), this);
    auto *bindingsSection = makeSectionLabel(QStringLiteral("Replacements & snippets"), this);

    auto *audioCard = makeSettingsCard(this);
    auto *audioLayout = qobject_cast<QVBoxLayout *>(audioCard->layout());
    auto *refinementCard = makeSettingsCard(this);
    auto *refinementLayout = qobject_cast<QVBoxLayout *>(refinementCard->layout());
    auto *outputCard = makeSettingsCard(this);
    auto *outputLayout = qobject_cast<QVBoxLayout *>(outputCard->layout());
    auto *openAiCard = makeSettingsCard(this);
    auto *openAiLayout = qobject_cast<QVBoxLayout *>(openAiCard->layout());
    auto *anthropicCard = makeSettingsCard(this);
    auto *anthropicLayout = qobject_cast<QVBoxLayout *>(anthropicCard->layout());

    m_ydotoolSetupButton = new QPushButton(QStringLiteral("Set up"), this);
    m_ydotoolStartButton = new QPushButton(QStringLiteral("Start service"), this);
    m_ydotoolDisableButton = new QPushButton(QStringLiteral("Disable in Speecher"), this);
    m_ydotoolRemoveButton = new QPushButton(QStringLiteral("Remove setup"), this);
    for (QPushButton *button : {m_ydotoolSetupButton, m_ydotoolStartButton, m_ydotoolDisableButton, m_ydotoolRemoveButton}) {
        button->setMinimumWidth(button->fontMetrics().horizontalAdvance(button->text()) + 36);
        button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    }
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

    addRow(outputLayout,
           makeRow(QStringLiteral("Method"),
                   QStringLiteral("How Speecher delivers final text."),
                   m_outputMethod,
                   outputCard),
           outputCard);
    addRow(outputLayout,
           makeRow(QStringLiteral("Format"),
                   QStringLiteral("Default clipboard representation. A CLI shortcut can override this per dictation."),
                   m_outputFormat,
                   outputCard),
           outputCard);
    addRow(outputLayout,
           makeRow(QStringLiteral("Global fallback"),
                   QStringLiteral("Paste behavior used unless a category or exact-app rule overrides it."),
                   m_globalPaste,
                   outputCard),
           outputCard);
    const QHash<AppCategory, QString> categoryLabels{
        {AppCategory::Terminal, QStringLiteral("Terminals")},
        {AppCategory::Browser, QStringLiteral("Browsers")},
        {AppCategory::Email, QStringLiteral("Email apps")},
        {AppCategory::Office, QStringLiteral("Office apps")},
        {AppCategory::CodeEditor, QStringLiteral("Code editors")},
        {AppCategory::General, QStringLiteral("Other apps")},
    };
    for (const auto &[category, combo] : std::as_const(m_categoryPasteControls)) {
        addRow(outputLayout,
               makeRow(categoryLabels.value(category),
                       QStringLiteral("Override the fallback for this application category."),
                       combo,
                       outputCard),
               outputCard);
    }

    auto *appRulesControl = new QWidget(outputCard);
    auto *appRulesLayout = new QVBoxLayout(appRulesControl);
    auto *appRulesTitle = new QLabel(QStringLiteral("App-specific paste rules"), appRulesControl);
    appRulesTitle->setObjectName(QStringLiteral("subsectionLabel"));
    auto *appRulesDescription = new QLabel(
        QStringLiteral("Override paste behavior for an exact application ID, such as org.kde.konsole."),
        appRulesControl);
    appRulesDescription->setObjectName(QStringLiteral("rowDescription"));
    appRulesDescription->setWordWrap(true);
    m_addAppPasteRuleButton = new QPushButton(QStringLiteral("Add rule"), appRulesControl);
    m_removeAppPasteRuleButton = new QPushButton(QStringLiteral("Delete selected"), appRulesControl);
    m_removeAppPasteRuleButton->setEnabled(false);
    auto *appRuleButtons = new QHBoxLayout;
    appRuleButtons->addStretch();
    appRuleButtons->addWidget(m_removeAppPasteRuleButton);
    appRuleButtons->addWidget(m_addAppPasteRuleButton);
    appRulesLayout->addWidget(appRulesTitle);
    appRulesLayout->addWidget(appRulesDescription);
    appRulesLayout->addWidget(m_appPasteRules);
    appRulesLayout->addLayout(appRuleButtons);
    outputLayout->addWidget(appRulesControl);
    outputLayout->addWidget(makeSeparator(outputCard));

    addRow(outputLayout,
           makeRow(QStringLiteral("Restore clipboard"),
                   QStringLiteral("Restore the previous clipboard only after insertion is verified."),
                   m_restoreClipboardAfterTyping,
                   outputCard),
           outputCard);
    addRow(outputLayout,
           makeRow(QStringLiteral("Virtual keyboard"),
                   QString(),
                   makeYdotoolControl(m_ydotoolStatus,
                                      m_ydotoolSetupButton,
                                      m_ydotoolStartButton,
                                      m_ydotoolDisableButton,
                                      m_ydotoolRemoveButton,
                                      outputCard),
                   outputCard),
           outputCard,
           false);

    addRow(refinementLayout, makeRow(QStringLiteral("Refinement"), QStringLiteral("Clean up dictated text after capture."), m_provider, refinementCard), refinementCard);
    addRow(refinementLayout, makeRow(QStringLiteral("Fallback profile"), QStringLiteral("Writing profile used when the target app does not imply one."), m_writingProfile, refinementCard), refinementCard);
    auto *profileSettingsControl = new QWidget(refinementCard);
    auto *profileSettingsLayout = new QVBoxLayout(profileSettingsControl);
    auto *profileSettingsTitle = new QLabel(QStringLiteral("Profile behavior"), profileSettingsControl);
    profileSettingsTitle->setObjectName(QStringLiteral("subsectionLabel"));
    auto *profileSettingsDescription = new QLabel(
        QStringLiteral("Choose cleanup strength and an optional explicit tone for each automatically detected profile."),
        profileSettingsControl);
    profileSettingsDescription->setObjectName(QStringLiteral("rowDescription"));
    profileSettingsDescription->setWordWrap(true);
    profileSettingsLayout->addWidget(profileSettingsTitle);
    profileSettingsLayout->addWidget(profileSettingsDescription);
    profileSettingsLayout->addWidget(m_profileSettings);
    refinementLayout->addWidget(profileSettingsControl);
    refinementLayout->addWidget(makeSeparator(refinementCard));

    auto *profileOverridesControl = new QWidget(refinementCard);
    auto *profileOverridesLayout = new QVBoxLayout(profileOverridesControl);
    auto *profileOverridesTitle = new QLabel(QStringLiteral("App-specific profile overrides"), profileOverridesControl);
    profileOverridesTitle->setObjectName(QStringLiteral("subsectionLabel"));
    auto *profileOverridesDescription = new QLabel(
        QStringLiteral("An exact application ID overrides automatic category detection and the fallback profile."),
        profileOverridesControl);
    profileOverridesDescription->setObjectName(QStringLiteral("rowDescription"));
    profileOverridesDescription->setWordWrap(true);
    m_addAppProfileOverrideButton = new QPushButton(QStringLiteral("Add override"), profileOverridesControl);
    m_removeAppProfileOverrideButton = new QPushButton(QStringLiteral("Delete selected"), profileOverridesControl);
    m_removeAppProfileOverrideButton->setEnabled(false);
    auto *profileOverrideButtons = new QHBoxLayout;
    profileOverrideButtons->addStretch();
    profileOverrideButtons->addWidget(m_removeAppProfileOverrideButton);
    profileOverrideButtons->addWidget(m_addAppProfileOverrideButton);
    profileOverridesLayout->addWidget(profileOverridesTitle);
    profileOverridesLayout->addWidget(profileOverridesDescription);
    profileOverridesLayout->addWidget(m_appProfileOverrides);
    profileOverridesLayout->addLayout(profileOverrideButtons);
    refinementLayout->addWidget(profileOverridesControl);
    refinementLayout->addWidget(makeSeparator(refinementCard));

    addRow(refinementLayout, makeRow(QStringLiteral("Target context"), QStringLiteral("Use the focused app, control, selection, and bounded nearby text for cleanup."), m_useTargetContext, refinementCard), refinementCard);
    addRow(refinementLayout, makeRow(QStringLiteral("Screenshot context"), QStringLiteral("Off by default; used only with image-capable refinement models."), m_screenshotContext, refinementCard), refinementCard, false);

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

    auto *correctionsCard = new QFrame(this);
    correctionsCard->setObjectName(QStringLiteral("vocabSection"));
    auto *correctionsLayout = new QVBoxLayout(correctionsCard);
    auto *correctionsDescription = new QLabel(
        QStringLiteral("Source-marked corrections learned after verified insertion. Edit, disable, delete, or undo deletions here."),
        correctionsCard);
    correctionsDescription->setObjectName(QStringLiteral("rowDescription"));
    correctionsDescription->setWordWrap(true);
    m_removeCorrectionButton = new QPushButton(QStringLiteral("Delete selected"), correctionsCard);
    m_undoCorrectionButton = new QPushButton(QStringLiteral("Undo delete"), correctionsCard);
    m_undoLatestLearnButton = new QPushButton(QStringLiteral("Undo latest learn"), correctionsCard);
    m_undoCorrectionButton->setEnabled(false);
    auto *correctionButtons = new QHBoxLayout;
    correctionButtons->addWidget(m_learnCorrections);
    correctionButtons->addStretch();
    correctionButtons->addWidget(m_undoLatestLearnButton);
    correctionButtons->addWidget(m_undoCorrectionButton);
    correctionButtons->addWidget(m_removeCorrectionButton);
    correctionsLayout->addWidget(correctionsDescription);
    correctionsLayout->addWidget(m_corrections);
    correctionsLayout->addLayout(correctionButtons);

    auto *bindingSection = new QFrame(this);
    bindingSection->setObjectName(QStringLiteral("bindingSection"));
    auto *bindingLayout = new QVBoxLayout(bindingSection);
    auto *bindingTitle = new QLabel(QStringLiteral("Replacements & snippets"), bindingSection);
    bindingTitle->setObjectName(QStringLiteral("subsectionLabel"));
    bindingTitle->setForegroundRole(QPalette::WindowText);
    bindingTitle->setAttribute(Qt::WA_StyledBackground, false);
    auto *bindingDescription = new QLabel(
        QStringLiteral("Replace a spoken phrase with exact text, including multi-line snippets. Matching ignores case and treats punctuation as spaces."),
        bindingSection);
    bindingDescription->setObjectName(QStringLiteral("rowDescription"));
    bindingDescription->setWordWrap(true);
    bindingDescription->setAttribute(Qt::WA_StyledBackground, false);
    m_addBindingButton = new QPushButton(QStringLiteral("Add replacement"), bindingSection);
    m_importSnippetsButton = new QPushButton(QStringLiteral("Import snippets JSON"), bindingSection);
    m_addBindingButton->setIcon(QIcon::fromTheme(QStringLiteral("list-add")));
    auto *bindingButtons = new QHBoxLayout;
    bindingButtons->addStretch();
    bindingButtons->addWidget(m_importSnippetsButton);
    bindingButtons->addWidget(m_addBindingButton);
    bindingLayout->addWidget(bindingTitle);
    bindingLayout->addWidget(bindingDescription);
    bindingLayout->addWidget(m_bindings);
    bindingLayout->addLayout(bindingButtons);

    auto *note = new QLabel(QStringLiteral("Automatic OpenAI auth follows the Codex auth mode when available, then falls back to Codex API key, Codex OAuth, OPENAI_API_KEY, and the app settings key. Codex OAuth uses the ChatGPT Codex backend. The app settings key is stored in the desktop keyring through QtKeychain when available."), this);
    note->setObjectName(QStringLiteral("noteText"));
    note->setWordWrap(true);
    note->setForegroundRole(QPalette::WindowText);
    note->setAttribute(Qt::WA_StyledBackground, false);

    m_generalPage = new GeneralSettingsPage(m_controller->primaryOutputStatus(), this);

    auto *audioPage = new QScrollArea(this);
    auto *audioPageLayout = makeSettingsPage(audioPage);
    audioPageLayout->addWidget(audioSection);
    audioPageLayout->addWidget(audioCard);
    audioPageLayout->addStretch();

    auto *outputPage = new QScrollArea(this);
    auto *outputPageLayout = makeSettingsPage(outputPage);
    outputPageLayout->addWidget(outputSection);
    outputPageLayout->addWidget(outputCard);
    outputPageLayout->addStretch();

    auto *refinementPage = new QScrollArea(this);
    auto *refinementPageLayout = makeSettingsPage(refinementPage);
    refinementPageLayout->addWidget(refinementSection);
    refinementPageLayout->addWidget(refinementCard);
    refinementPageLayout->addStretch();

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
    vocabularyPageLayout->addWidget(correctionsCard);
    vocabularyPageLayout->addWidget(bindingsSection);
    vocabularyPageLayout->addWidget(bindingSection);
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
        outputPage,
        refinementPage,
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
    connect(m_provider, &QComboBox::currentIndexChanged, this, [this] {
        updateScreenshotControl();
        updateButtonState();
    });
    connect(m_writingProfile, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateButtonState);
    connect(m_appProfileOverrides, &QTableWidget::itemChanged, this, &SettingsDialog::updateButtonState);
    connect(m_appProfileOverrides, &QTableWidget::itemSelectionChanged, this, [this] {
        m_removeAppProfileOverrideButton->setEnabled(m_appProfileOverrides->currentRow() >= 0);
    });
    connect(m_addAppProfileOverrideButton, &QPushButton::clicked, this, [this] {
        addWritingProfileOverride();
        updateButtonState();
    });
    connect(m_removeAppProfileOverrideButton, &QPushButton::clicked, this, [this] {
        const int row = m_appProfileOverrides->currentRow();
        if (row >= 0) {
            m_appProfileOverrides->removeRow(row);
            updateButtonState();
        }
    });
    connect(m_useTargetContext, &QCheckBox::toggled, this, &SettingsDialog::updateButtonState);
    connect(m_screenshotContext, &QCheckBox::toggled, this, &SettingsDialog::updateButtonState);
    connect(m_openAiModel, &QComboBox::currentTextChanged, this, &SettingsDialog::updateButtonState);
    connect(m_openAiEffort, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateButtonState);
    connect(m_anthropicModel, &QComboBox::currentTextChanged, this, [this] {
        updateAnthropicControls();
        updateButtonState();
    });
    connect(m_anthropicEffort, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateButtonState);
    connect(m_anthropicAuthMode, &QComboBox::currentIndexChanged, this, [this] {
        updateScreenshotControl();
        updateButtonState();
    });
    connect(m_anthropicInfoButton, &QPushButton::clicked, this, &SettingsDialog::showAnthropicAuthInfo);
    connect(m_restoreClipboardAfterTyping, &QCheckBox::toggled, this, &SettingsDialog::updateButtonState);
    connect(m_learnCorrections, &QCheckBox::toggled, this, &SettingsDialog::updateButtonState);
    connect(m_outputFormat, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateButtonState);
    connect(m_globalPaste, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateButtonState);
    for (const auto &[category, combo] : std::as_const(m_categoryPasteControls)) {
        Q_UNUSED(category);
        connect(combo, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateButtonState);
    }
    connect(m_appPasteRules, &QTableWidget::itemChanged, this, &SettingsDialog::updateButtonState);
    connect(m_appPasteRules, &QTableWidget::itemSelectionChanged, this, [this] {
        m_removeAppPasteRuleButton->setEnabled(m_appPasteRules->currentRow() >= 0);
    });
    connect(m_addAppPasteRuleButton, &QPushButton::clicked, this, [this] {
        addApplicationPasteRule();
        updateButtonState();
    });
    connect(m_removeAppPasteRuleButton, &QPushButton::clicked, this, [this] {
        const int row = m_appPasteRules->currentRow();
        if (row >= 0) {
            m_appPasteRules->removeRow(row);
            updateButtonState();
        }
    });
    connect(m_outputMethod, &QComboBox::currentIndexChanged, this, [this] {
        if (m_outputMethod->currentData().toString() == QString::fromLatin1(OutputMethod::Ydotool)) {
            const YdotoolSetupStatus status = YdotoolSetup::probe(m_controller->settings()->ydotoolEnabled());
            if (!status.ready() || !m_controller->settings()->ydotoolEnabled()) {
                QSignalBlocker blocker(m_outputMethod);
                selectData(m_outputMethod, m_controller->settings()->outputMethod());
                QToolTip::showText(m_outputMethod->mapToGlobal(m_outputMethod->rect().bottomLeft()),
                                   QStringLiteral("Set up ydotool first"),
                                   m_outputMethod);
                return;
            }
        }
        updateButtonState();
    });
    connect(m_authMode, &QComboBox::currentIndexChanged, this, [this] {
        updateAuthControl();
        updateButtonState();
    });
    connect(m_apiKey, &QLineEdit::textChanged, this, &SettingsDialog::updateButtonState);
    connect(m_corrections, &QTableWidget::itemChanged, this, &SettingsDialog::updateButtonState);
    connect(m_corrections, &QTableWidget::itemSelectionChanged, this, [this] {
        m_removeCorrectionButton->setEnabled(m_corrections->currentRow() >= 0);
    });
    connect(m_removeCorrectionButton, &QPushButton::clicked, this, [this] {
        const int row = m_corrections->currentRow();
        QList<LearnedCorrection> corrections = currentLearnedCorrections();
        if (row < 0 || row >= corrections.size()) {
            return;
        }
        m_removedCorrections.append(corrections.takeAt(row));
        setLearnedCorrections(corrections);
        m_undoCorrectionButton->setEnabled(true);
        updateButtonState();
    });
    connect(m_undoCorrectionButton, &QPushButton::clicked, this, [this] {
        if (m_removedCorrections.isEmpty()) {
            return;
        }
        QList<LearnedCorrection> corrections = currentLearnedCorrections();
        corrections.prepend(m_removedCorrections.takeLast());
        setLearnedCorrections(corrections);
        m_undoCorrectionButton->setEnabled(!m_removedCorrections.isEmpty());
        updateButtonState();
    });
    connect(m_undoLatestLearnButton, &QPushButton::clicked, this, [this] {
        QList<LearnedCorrection> corrections = currentLearnedCorrections();
        if (corrections.isEmpty()) {
            return;
        }
        m_removedCorrections.append(corrections.takeFirst());
        setLearnedCorrections(corrections);
        m_undoCorrectionButton->setEnabled(true);
        updateButtonState();
    });
    connect(m_ydotoolSetupButton, &QPushButton::clicked, this, &SettingsDialog::setupOrEnableYdotool);
    connect(m_ydotoolStartButton, &QPushButton::clicked, this, [this] {
        QString error;
        if (!YdotoolSetup::startUserService(&error)) {
            QMessageBox::warning(this, QStringLiteral("ydotool service"), error);
        }
        refreshOutputControls();
    });
    connect(m_ydotoolDisableButton, &QPushButton::clicked, this, &SettingsDialog::disableYdotool);
    connect(m_ydotoolRemoveButton, &QPushButton::clicked, this, &SettingsDialog::removeYdotoolSetup);
    connect(m_vocabularyPage, &VocabularySettingsPage::changed, this, &SettingsDialog::updateButtonState);
    connect(m_addBindingButton, &QPushButton::clicked, this, [this] {
        editBinding(-1);
    });
    connect(m_importSnippetsButton, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this,
            QStringLiteral("Import snippets"),
            QString(),
            QStringLiteral("JSON files (*.json);;All files (*)"));
        if (path.isEmpty()) {
            return;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this,
                                 QStringLiteral("Snippets not imported"),
                                 QStringLiteral("Could not read %1.").arg(path));
            return;
        }
        QString error;
        const QList<BindingRule> imported = BindingProcessor::parseJsonImport(file.readAll(), &error);
        if (!error.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Snippets not imported"), error);
            return;
        }
        const BindingValidationResult merged = BindingProcessor::validateRules(m_bindingRules + imported);
        if (!merged.ok()) {
            QMessageBox::warning(this,
                                 QStringLiteral("Snippets not imported"),
                                 merged.messages().join(QStringLiteral("\n")));
            return;
        }
        m_bindingRules = merged.rules;
        refreshBindingList();
        updateButtonState();
    });
    connect(m_bindings, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (item) {
            editBinding(item->data(Qt::UserRole).toInt());
        }
    });
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
    selectData(m_provider, settings->refinementProvider());
    selectData(m_writingProfile, settings->defaultWritingProfile());
    setWritingProfileSettings(settings->writingProfileSettings());
    setWritingProfileOverrides(settings->writingProfileOverrides());
    m_useTargetContext->setChecked(settings->useTargetContext());
    m_screenshotContext->setChecked(settings->includeScreenshotContext());
    selectEditableText(m_openAiModel, settings->openAiModel());
    selectData(m_openAiEffort, settings->openAiEffort());
    selectEditableText(m_anthropicModel, settings->anthropicModel());
    selectData(m_anthropicEffort, settings->anthropicEffort());
    selectData(m_outputMethod, settings->outputMethod());
    selectData(m_outputFormat, outputFormatName(settings->outputFormat()));
    const QList<PasteRule> pasteRules = settings->pasteRules();
    selectData(m_globalPaste,
               pasteMethodName(pasteMethodFor(
                   pasteRules,
                   PasteRuleScope::Global,
                   QString(),
                   PasteMethod::StandardPaste)));
    for (const auto &[category, combo] : std::as_const(m_categoryPasteControls)) {
        QString method = QStringLiteral("inherit");
        for (const PasteRule &rule : pasteRules) {
            if (rule.scope == PasteRuleScope::Category
                && rule.match == appCategoryName(category)) {
                method = pasteMethodName(rule.method);
                break;
            }
        }
        selectData(combo, method);
    }
    setApplicationPasteRules(pasteRules);
    m_restoreClipboardAfterTyping->setChecked(settings->restoreClipboardAfterTyping());
    selectData(m_authMode, settings->openAiAuthMode());
    selectData(m_anthropicAuthMode, settings->anthropicAuthMode());
    m_apiKey->setText(m_controller->secretStore()->apiKey());
    m_vocabularyPage->load(settings->vocabularyEntries());
    setBindingRules(settings->bindingRules());
    m_learnCorrections->setChecked(settings->correctionLearningEnabled());
    m_removedCorrections.clear();
    m_undoCorrectionButton->setEnabled(false);
    setLearnedCorrections(settings->learnedCorrections());
    m_undoLatestLearnButton->setEnabled(!settings->learnedCorrections().isEmpty());
    updateAudioControls();
    updateAuthControl();
    updateAnthropicControls();
    updateScreenshotControl();
    refreshOutputControls();
    updateButtonState();
}

bool SettingsDialog::save()
{
    SettingsStore *settings = m_controller->settings();
    const QList<BindingRule> bindingRules = currentBindingRules();
    const BindingValidationResult bindingValidation = BindingProcessor::validateRules(bindingRules);
    if (!bindingValidation.ok()) {
        QMessageBox::warning(this,
                             QStringLiteral("Replacements not saved"),
                             bindingValidation.messages().join(QStringLiteral("\n")));
        return false;
    }
    const QList<PasteRule> applicationPasteRules = currentApplicationPasteRules();
    QSet<QString> applicationIds;
    for (const PasteRule &rule : applicationPasteRules) {
        const QString id = rule.match.toCaseFolded();
        if (applicationIds.contains(id)) {
            QMessageBox::warning(this,
                                 QStringLiteral("Paste rules not saved"),
                                 QStringLiteral("Each application ID can have only one paste rule."));
            return false;
        }
        applicationIds.insert(id);
    }
    const QList<WritingProfileOverride> profileOverrides = currentWritingProfileOverrides();
    QSet<QString> profileApplicationIds;
    for (const WritingProfileOverride &override : profileOverrides) {
        const QString id = override.applicationId.toCaseFolded();
        if (profileApplicationIds.contains(id)) {
            QMessageBox::warning(this,
                                 QStringLiteral("Writing profiles not saved"),
                                 QStringLiteral("Each application ID can have only one Writing Profile override."));
            return false;
        }
        profileApplicationIds.insert(id);
    }

    AppSettings draft = settings->snapshot();
    m_generalPage->appendToDraft(draft);
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
    settings->setRefinementProvider(m_provider->currentData().toString());
    settings->setDefaultWritingProfile(m_writingProfile->currentData().toString());
    settings->setWritingProfileSettings(currentWritingProfileSettings());
    settings->setWritingProfileOverrides(profileOverrides);
    settings->setUseTargetContext(m_useTargetContext->isChecked());
    settings->setIncludeScreenshotContext(m_screenshotContext->isChecked());
    settings->setOpenAiModel(editableComboValue(m_openAiModel));
    selectEditableText(m_openAiModel, settings->openAiModel());
    settings->setOpenAiEffort(m_openAiEffort->currentData().toString());
    settings->setAnthropicModel(editableComboValue(m_anthropicModel));
    selectEditableText(m_anthropicModel, settings->anthropicModel());
    settings->setAnthropicEffort(m_anthropicEffort->currentData().toString());
    settings->setOutputMethod(m_outputMethod->currentData().toString());
    settings->setOutputFormat(outputFormatFromString(m_outputFormat->currentData().toString()));
    settings->setPasteRules(withPasteRules(
        settings->pasteRules(),
        applicationPasteRules,
        currentCategoryPasteRules(),
        pasteMethodFromName(m_globalPaste->currentData().toString())));
    settings->setRestoreClipboardAfterTyping(m_restoreClipboardAfterTyping->isChecked());
    settings->setOpenAiAuthMode(m_authMode->currentData().toString());
    settings->setAnthropicAuthMode(m_anthropicAuthMode->currentData().toString());
    settings->setVocabularyEntries(m_vocabularyPage->entries());
    settings->setCorrectionLearningEnabled(m_learnCorrections->isChecked());
    settings->setLearnedCorrections(currentLearnedCorrections());
    settings->setBindingRules(bindingValidation.rules);
    m_vocabularyPage->load(settings->vocabularyEntries());
    setBindingRules(settings->bindingRules());
    setLearnedCorrections(settings->learnedCorrections());
    m_removedCorrections.clear();
    m_undoCorrectionButton->setEnabled(false);
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
    updateScreenshotControl();
    refreshOutputControls();
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
        || m_provider->currentData().toString() != settings->refinementProvider()
        || m_writingProfile->currentData().toString() != settings->defaultWritingProfile()
        || currentWritingProfileSettings() != settings->writingProfileSettings()
        || currentWritingProfileOverrides() != settings->writingProfileOverrides()
        || m_useTargetContext->isChecked() != settings->useTargetContext()
        || m_screenshotContext->isChecked() != settings->includeScreenshotContext()
        || editableComboValue(m_openAiModel) != settings->openAiModel()
        || m_openAiEffort->currentData().toString() != settings->openAiEffort()
        || editableComboValue(m_anthropicModel) != settings->anthropicModel()
        || m_anthropicEffort->currentData().toString() != settings->anthropicEffort()
        || m_outputMethod->currentData().toString() != settings->outputMethod()
        || m_outputFormat->currentData().toString() != outputFormatName(settings->outputFormat())
        || withPasteRules(
               settings->pasteRules(),
               currentApplicationPasteRules(),
               currentCategoryPasteRules(),
               pasteMethodFromName(m_globalPaste->currentData().toString()))
            != settings->pasteRules()
        || m_restoreClipboardAfterTyping->isChecked() != settings->restoreClipboardAfterTyping()
        || m_authMode->currentData().toString() != settings->openAiAuthMode()
        || m_anthropicAuthMode->currentData().toString() != settings->anthropicAuthMode()
        || m_vocabularyPage->hasChanges(settings->vocabularyEntries())
        || m_learnCorrections->isChecked() != settings->correctionLearningEnabled()
        || currentLearnedCorrections() != settings->learnedCorrections()
        || currentBindingRules() != settings->bindingRules()) {
        return true;
    }

    return m_authMode->currentData().toString() == QStringLiteral("settings")
        && m_apiKey->text().trimmed() != m_controller->secretStore()->apiKey();
}

QList<LearnedCorrection> SettingsDialog::currentLearnedCorrections() const
{
    QList<LearnedCorrection> corrections;
    for (int row = 0; row < m_corrections->rowCount(); ++row) {
        const QTableWidgetItem *enabled = m_corrections->item(row, 0);
        const QTableWidgetItem *original = m_corrections->item(row, 1);
        const QTableWidgetItem *corrected = m_corrections->item(row, 2);
        const QTableWidgetItem *application = m_corrections->item(row, 3);
        if (!enabled || !original || !corrected || !application) {
            continue;
        }
        LearnedCorrection correction;
        correction.id = enabled->data(Qt::UserRole).toString();
        correction.createdAtMs = enabled->data(Qt::UserRole + 1).toLongLong();
        correction.confidence = enabled->data(Qt::UserRole + 2).toDouble();
        correction.enabled = enabled->checkState() == Qt::Checked;
        correction.original = original->text().trimmed();
        correction.corrected = corrected->text().trimmed();
        correction.applicationId = application->text().trimmed();
        if (!correction.id.isEmpty() && !correction.original.isEmpty() && !correction.corrected.isEmpty()) {
            corrections.append(correction);
        }
    }
    return corrections;
}

QList<PasteRule> SettingsDialog::currentApplicationPasteRules() const
{
    QList<PasteRule> rules;
    for (int row = 0; row < m_appPasteRules->rowCount(); ++row) {
        const QTableWidgetItem *enabled = m_appPasteRules->item(row, 0);
        const QTableWidgetItem *application = m_appPasteRules->item(row, 1);
        const auto *method = qobject_cast<QComboBox *>(m_appPasteRules->cellWidget(row, 2));
        const QString applicationId = application ? application->text().trimmed() : QString();
        if (applicationId.isEmpty() || !method) {
            continue;
        }
        rules.append({
            PasteRuleScope::Application,
            applicationId,
            pasteMethodFromName(method->currentData().toString()),
            enabled && enabled->checkState() == Qt::Checked,
        });
    }
    return rules;
}

QList<PasteRule> SettingsDialog::currentCategoryPasteRules() const
{
    QList<PasteRule> rules;
    for (const auto &[category, combo] : m_categoryPasteControls) {
        if (combo->currentData().toString() == QStringLiteral("inherit")) {
            continue;
        }
        rules.append({
            PasteRuleScope::Category,
            appCategoryName(category),
            pasteMethodFromName(combo->currentData().toString()),
            true,
        });
    }
    return rules;
}

QList<WritingProfileSettings> SettingsDialog::currentWritingProfileSettings() const
{
    QList<WritingProfileSettings> settings;
    for (int row = 0; row < m_profileSettings->rowCount(); ++row) {
        const QTableWidgetItem *profileItem = m_profileSettings->item(row, 0);
        const auto *strength = qobject_cast<QComboBox *>(m_profileSettings->cellWidget(row, 1));
        const auto *tone = qobject_cast<QComboBox *>(m_profileSettings->cellWidget(row, 2));
        if (!profileItem || !strength || !tone) {
            continue;
        }
        settings.append({
            writingProfileFromName(profileItem->data(Qt::UserRole).toString()),
            strength->currentData().toString(),
            tone->currentData().toString(),
        });
    }
    return settings;
}

QList<WritingProfileOverride> SettingsDialog::currentWritingProfileOverrides() const
{
    QList<WritingProfileOverride> overrides;
    for (int row = 0; row < m_appProfileOverrides->rowCount(); ++row) {
        const QTableWidgetItem *enabled = m_appProfileOverrides->item(row, 0);
        const QTableWidgetItem *application = m_appProfileOverrides->item(row, 1);
        const auto *profile = qobject_cast<QComboBox *>(m_appProfileOverrides->cellWidget(row, 2));
        const QString applicationId = application ? application->text().trimmed() : QString();
        if (applicationId.isEmpty() || !profile) {
            continue;
        }
        overrides.append({
            applicationId,
            writingProfileFromName(profile->currentData().toString()),
            enabled && enabled->checkState() == Qt::Checked,
        });
    }
    return overrides;
}

void SettingsDialog::setWritingProfileSettings(const QList<WritingProfileSettings> &settings)
{
    QSignalBlocker blocker(m_profileSettings);
    m_profileSettings->setRowCount(0);
    for (const WritingProfileSettings &fallback : defaultWritingProfileSettings()) {
        const WritingProfileSettings profileSettings = writingProfileSettingsFor(settings, fallback.profile);
        const int row = m_profileSettings->rowCount();
        m_profileSettings->insertRow(row);
        auto *profile = new QTableWidgetItem(writingProfileLabel(fallback.profile));
        profile->setFlags(Qt::ItemIsEnabled);
        profile->setData(Qt::UserRole, writingProfileName(fallback.profile));
        auto *strength = new QComboBox(m_profileSettings);
        addCleanupStrengths(strength);
        selectData(strength, profileSettings.cleanupStrength);
        auto *tone = new QComboBox(m_profileSettings);
        addWritingTones(tone);
        selectData(tone, profileSettings.tone);
        connect(strength, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateButtonState);
        connect(tone, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateButtonState);
        m_profileSettings->setItem(row, 0, profile);
        m_profileSettings->setCellWidget(row, 1, strength);
        m_profileSettings->setCellWidget(row, 2, tone);
    }
}

void SettingsDialog::setWritingProfileOverrides(const QList<WritingProfileOverride> &overrides)
{
    QSignalBlocker blocker(m_appProfileOverrides);
    m_appProfileOverrides->setRowCount(0);
    for (const WritingProfileOverride &override : overrides) {
        addWritingProfileOverride(override);
    }
    m_removeAppProfileOverrideButton->setEnabled(false);
}

void SettingsDialog::addWritingProfileOverride(const WritingProfileOverride &override)
{
    const int row = m_appProfileOverrides->rowCount();
    m_appProfileOverrides->insertRow(row);
    auto *enabled = new QTableWidgetItem;
    enabled->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    enabled->setCheckState(override.enabled ? Qt::Checked : Qt::Unchecked);
    auto *application = new QTableWidgetItem(override.applicationId);
    application->setToolTip(QStringLiteral("Use the desktop application ID reported by AT-SPI."));
    auto *profile = new QComboBox(m_appProfileOverrides);
    addWritingProfiles(profile);
    selectData(profile, writingProfileName(override.profile));
    connect(profile, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateButtonState);
    m_appProfileOverrides->setItem(row, 0, enabled);
    m_appProfileOverrides->setItem(row, 1, application);
    m_appProfileOverrides->setCellWidget(row, 2, profile);
    if (override.applicationId.isEmpty()) {
        m_appProfileOverrides->setCurrentCell(row, 1);
        m_appProfileOverrides->editItem(application);
    }
}

void SettingsDialog::setApplicationPasteRules(const QList<PasteRule> &rules)
{
    QSignalBlocker blocker(m_appPasteRules);
    m_appPasteRules->setRowCount(0);
    for (const PasteRule &rule : rules) {
        if (rule.scope == PasteRuleScope::Application) {
            addApplicationPasteRule(rule);
        }
    }
    m_removeAppPasteRuleButton->setEnabled(false);
}

void SettingsDialog::addApplicationPasteRule(const PasteRule &rule)
{
    const int row = m_appPasteRules->rowCount();
    m_appPasteRules->insertRow(row);

    auto *enabled = new QTableWidgetItem;
    enabled->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    enabled->setCheckState(rule.enabled ? Qt::Checked : Qt::Unchecked);
    auto *application = new QTableWidgetItem(rule.match);
    application->setToolTip(QStringLiteral("Use the desktop application ID reported by AT-SPI."));
    auto *method = new QComboBox(m_appPasteRules);
    addPasteMethods(method, true);
    selectData(method, pasteMethodName(rule.method));
    connect(method, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateButtonState);

    m_appPasteRules->setItem(row, 0, enabled);
    m_appPasteRules->setItem(row, 1, application);
    m_appPasteRules->setCellWidget(row, 2, method);
    if (rule.match.isEmpty()) {
        m_appPasteRules->setCurrentCell(row, 1);
        m_appPasteRules->editItem(application);
    }
}

void SettingsDialog::setLearnedCorrections(const QList<LearnedCorrection> &corrections)
{
    QSignalBlocker blocker(m_corrections);
    m_corrections->setRowCount(0);
    for (const LearnedCorrection &correction : corrections) {
        const int row = m_corrections->rowCount();
        m_corrections->insertRow(row);
        auto *enabled = new QTableWidgetItem;
        enabled->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        enabled->setCheckState(correction.enabled ? Qt::Checked : Qt::Unchecked);
        enabled->setData(Qt::UserRole, correction.id);
        enabled->setData(Qt::UserRole + 1, correction.createdAtMs);
        enabled->setData(Qt::UserRole + 2, correction.confidence);
        auto *original = new QTableWidgetItem(correction.original);
        auto *corrected = new QTableWidgetItem(correction.corrected);
        auto *application = new QTableWidgetItem(correction.applicationId);
        application->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        application->setToolTip(QStringLiteral("Learned automatically · confidence %1%")
                                    .arg(qRound(correction.confidence * 100.0)));
        m_corrections->setItem(row, 0, enabled);
        m_corrections->setItem(row, 1, original);
        m_corrections->setItem(row, 2, corrected);
        m_corrections->setItem(row, 3, application);
    }
    m_removeCorrectionButton->setEnabled(false);
    if (m_undoLatestLearnButton) {
        m_undoLatestLearnButton->setEnabled(!corrections.isEmpty());
    }
}

QList<BindingRule> SettingsDialog::currentBindingRules() const
{
    return m_bindingRules;
}

void SettingsDialog::setBindingRules(const QList<BindingRule> &rules)
{
    m_bindingRules = rules;
    refreshBindingList();
}

void SettingsDialog::refreshBindingList()
{
    QScrollBar *scrollBar = m_scroll ? m_scroll->verticalScrollBar() : nullptr;
    const int scrollValue = scrollBar ? scrollBar->value() : 0;

    QSignalBlocker blocker(m_bindings);
    m_bindings->clear();

    for (int row = 0; row < m_bindingRules.size(); ++row) {
        const BindingRule rule = m_bindingRules.at(row);
        auto *item = new QListWidgetItem(m_bindings);
        item->setData(Qt::UserRole, row);
        item->setSizeHint(QSize(0, 56));

        auto *rowWidget = new QWidget(m_bindings);
        rowWidget->setObjectName(QStringLiteral("bindingRow"));
        auto *layout = new QHBoxLayout(rowWidget);
        layout->setContentsMargins(10, 6, 8, 6);
        layout->setSpacing(8);

        auto *phrase = new ElidedLabel(rowWidget);
        phrase->setText(rule.phrase);
        phrase->setToolTip(rule.phrase);
        phrase->setMinimumWidth(120);
        phrase->setForegroundRole(QPalette::WindowText);
        QFont phraseFont = phrase->font();
        phraseFont.setBold(true);
        phrase->setFont(phraseFont);

        auto *arrow = new QLabel(rowWidget);
        arrow->setAlignment(Qt::AlignCenter);
        arrow->setPixmap(style()->standardIcon(QStyle::SP_ArrowRight).pixmap(16, 16));
        arrow->setFixedWidth(18);

        auto *preview = new ElidedLabel(rowWidget);
        preview->setText(bindingPreview(rule.replacement));
        preview->setToolTip(rule.replacement);
        preview->setForegroundRole(QPalette::WindowText);

        auto *edit = new QPushButton(QStringLiteral("Edit"), rowWidget);
        edit->setIcon(QIcon::fromTheme(QStringLiteral("document-edit")));
        edit->setMinimumWidth(edit->fontMetrics().horizontalAdvance(edit->text()) + 32);
        edit->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

        auto *remove = new QPushButton(QStringLiteral("Remove"), rowWidget);
        remove->setIcon(QIcon::fromTheme(QStringLiteral("edit-delete")));
        remove->setMinimumWidth(remove->fontMetrics().horizontalAdvance(remove->text()) + 32);
        remove->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

        layout->addWidget(phrase, 1);
        layout->addWidget(arrow, 0);
        layout->addWidget(preview, 4);
        layout->addWidget(edit, 0);
        layout->addWidget(remove, 0);

        m_bindings->setItemWidget(item, rowWidget);

        connect(edit, &QPushButton::clicked, this, [this, row] {
            editBinding(row);
        });
        connect(remove, &QPushButton::clicked, this, [this, row] {
            if (row < 0 || row >= m_bindingRules.size()) {
                return;
            }
            m_bindingRules.removeAt(row);
            refreshBindingList();
            updateButtonState();
        });
    }

    if (scrollBar) {
        scrollBar->setValue(qMin(scrollValue, scrollBar->maximum()));
        QTimer::singleShot(0, this, [this, scrollValue] {
            QScrollBar *delayedScrollBar = m_scroll ? m_scroll->verticalScrollBar() : nullptr;
            if (delayedScrollBar) {
                delayedScrollBar->setValue(qMin(scrollValue, delayedScrollBar->maximum()));
            }
        });
    }
}

void SettingsDialog::editBinding(int row)
{
    const bool editing = row >= 0 && row < m_bindingRules.size();

    QDialog dialog(this);
    dialog.setWindowTitle(editing ? QStringLiteral("Edit replacement") : QStringLiteral("Add replacement"));
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 18, 18, 14);
    layout->setSpacing(8);

    auto *phraseLabel = new QLabel(QStringLiteral("Spoken phrase"), &dialog);
    auto *phrase = new QLineEdit(&dialog);
    phrase->setClearButtonEnabled(true);

    auto *replacementLabel = new QLabel(QStringLiteral("Exact replacement or snippet"), &dialog);
    auto *replacement = new QPlainTextEdit(&dialog);
    replacement->setMinimumHeight(240);
    replacement->setLineWrapMode(QPlainTextEdit::WidgetWidth);

    if (editing) {
        phrase->setText(m_bindingRules.at(row).phrase);
        replacement->setPlainText(m_bindingRules.at(row).replacement);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QPushButton *saveButton = buttons->button(QDialogButtonBox::Ok);
    if (saveButton) {
        saveButton->setText(QStringLiteral("Save"));
        saveButton->setIcon(QIcon::fromTheme(QStringLiteral("document-save")));
    }
    QPushButton *deleteButton = nullptr;
    if (editing) {
        deleteButton = buttons->addButton(QStringLiteral("Delete"), QDialogButtonBox::DestructiveRole);
        deleteButton->setIcon(QIcon::fromTheme(QStringLiteral("edit-delete")));
    }

    layout->addWidget(phraseLabel);
    layout->addWidget(phrase);
    layout->addSpacing(8);
    layout->addWidget(replacementLabel);
    layout->addWidget(replacement, 1);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (saveButton) {
        connect(saveButton, &QPushButton::clicked, &dialog, [this, row, editing, phrase, replacement, &dialog] {
            QList<BindingRule> candidate = m_bindingRules;
            const BindingRule updated{phrase->text().trimmed(), replacement->toPlainText()};
            if (editing) {
                candidate[row] = updated;
            } else {
                candidate.append(updated);
            }

            const BindingValidationResult validation = BindingProcessor::validateRules(candidate);
            if (!validation.ok()) {
                QMessageBox::warning(&dialog,
                                     QStringLiteral("Replacement not saved"),
                                     validation.messages().join(QStringLiteral("\n")));
                return;
            }

            m_bindingRules = validation.rules;
            refreshBindingList();
            updateButtonState();
            dialog.accept();
        });
    }
    if (deleteButton) {
        connect(deleteButton, &QPushButton::clicked, &dialog, [this, row, &dialog] {
            if (row >= 0 && row < m_bindingRules.size()) {
                m_bindingRules.removeAt(row);
                refreshBindingList();
                updateButtonState();
            }
            dialog.accept();
        });
    }

    dialog.resize(560, 430);
    phrase->setFocus(Qt::OtherFocusReason);
    dialog.exec();
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

void SettingsDialog::updateScreenshotControl()
{
    const QString provider = m_provider->currentData().toString();
    const bool supported = provider == QStringLiteral("openai")
        || provider == QStringLiteral("anthropic");
    m_screenshotContext->setEnabled(supported);
    m_screenshotContext->setToolTip(
        supported
            ? QStringLiteral("Captured through the desktop portal and kept only for the current dictation.")
            : QStringLiteral("Choose an image-capable OpenAI or Anthropic refiner to send screenshot context."));
}

void SettingsDialog::showAnthropicAuthInfo()
{
    QMessageBox::information(
        this,
        QStringLiteral("Anthropic auth"),
        QStringLiteral("Speecher reads the existing Claude Code OAuth session from ~/.claude/.credentials.json and calls the Anthropic Messages API directly. It does not start or control a Claude Code agent session."));
}

void SettingsDialog::refreshOutputControls()
{
    const YdotoolSetupStatus status = YdotoolSetup::probe(m_controller->settings()->ydotoolEnabled());
    const bool ydotoolEnabled = m_controller->settings()->ydotoolEnabled() && status.ready();
    const int ydotoolIndex = m_outputMethod->findData(QString::fromLatin1(OutputMethod::Ydotool));
    setComboItemEnabled(m_outputMethod,
                        ydotoolIndex,
                        ydotoolEnabled,
                        ydotoolEnabled ? QString() : QStringLiteral("Set up ydotool first"));
    if (!ydotoolEnabled && m_outputMethod->currentData().toString() == QString::fromLatin1(OutputMethod::Ydotool)) {
        QSignalBlocker blocker(m_outputMethod);
        selectData(m_outputMethod, QString::fromLatin1(OutputMethod::Automatic));
    }
    m_outputMethod->setToolTip(ydotoolEnabled
                                   ? QStringLiteral("Automatic tries ydotool paste, wl-copy, then Qt clipboard.")
                                   : QStringLiteral("Type with ydotool paste is disabled until virtual keyboard setup passes."));
    m_ydotoolStatus->setText(status.label + QStringLiteral(". ") + status.detail);
    updateYdotoolButtons();
}

void SettingsDialog::updateYdotoolButtons()
{
    const YdotoolSetupStatus status = YdotoolSetup::probe(m_controller->settings()->ydotoolEnabled());
    const bool ready = status.ready();
    const bool disabled = status.state == YdotoolSetupState::Disabled;
    const bool daemonMissing = status.state == YdotoolSetupState::DaemonNotRunning;
    const bool setupInstalled = status.speecherManagedSetupInstalled || ready || disabled;
    const QString setupFirst = QStringLiteral("Run setup first");
    m_ydotoolSetupButton->setText(disabled && status.speecherManagedSetupInstalled ? QStringLiteral("Enable") : QStringLiteral("Set up"));
    m_ydotoolSetupButton->setVisible(!ready || disabled);
    m_ydotoolSetupButton->setEnabled(status.state != YdotoolSetupState::NeedsSignOut);
    m_ydotoolStartButton->setVisible(daemonMissing);
    m_ydotoolStartButton->setEnabled(setupInstalled);
    m_ydotoolStartButton->setToolTip(setupInstalled ? QString() : setupFirst);
    m_ydotoolDisableButton->setVisible(ready && m_controller->settings()->ydotoolEnabled());
    m_ydotoolRemoveButton->setVisible(status.speecherManagedSetupInstalled);
    m_ydotoolRemoveButton->setEnabled(status.speecherManagedSetupInstalled);
    m_ydotoolRemoveButton->setToolTip(setupInstalled ? QString() : setupFirst);
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

void SettingsDialog::setupOrEnableYdotool()
{
    const YdotoolSetupStatus status = YdotoolSetup::probe(m_controller->settings()->ydotoolEnabled());
    if (status.state == YdotoolSetupState::Disabled && status.speecherManagedSetupInstalled) {
        if (verifyYdotoolTyping()) {
            m_controller->settings()->setYdotoolEnabled(true);
            refreshOutputControls();
            updateButtonState();
        }
        return;
    }

    const int answer = QMessageBox::question(this,
                                             QStringLiteral("Set up virtual keyboard"),
                                             QStringLiteral("Speecher will ask for administrator permission to install ydotool if needed, load uinput, configure a speecher-uinput group, install udev rules, and install a user-level ydotoold service. Speecher itself remains unprivileged at runtime."),
                                             QMessageBox::Cancel | QMessageBox::Ok,
                                             QMessageBox::Ok);
    if (answer != QMessageBox::Ok) {
        return;
    }

    QString error;
    if (!YdotoolSetup::runHelper(YdotoolSetup::HelperAction::Install, &error)) {
        QMessageBox::warning(this, QStringLiteral("ydotool setup failed"), error);
        refreshOutputControls();
        return;
    }
    if (!YdotoolSetup::startUserService(&error)) {
        QMessageBox::warning(this, QStringLiteral("ydotool service"), error);
    }
    if (verifyYdotoolTyping()) {
        m_controller->settings()->setYdotoolEnabled(true);
    }
    refreshOutputControls();
    updateButtonState();
}

void SettingsDialog::disableYdotool()
{
    m_controller->settings()->setYdotoolEnabled(false);
    QSignalBlocker blocker(m_outputMethod);
    selectData(m_outputMethod, m_controller->settings()->outputMethod());
    refreshOutputControls();
    updateButtonState();
}

void SettingsDialog::removeYdotoolSetup()
{
    const int answer = QMessageBox::question(this,
                                             QStringLiteral("Remove virtual keyboard setup"),
                                             QStringLiteral("Speecher will ask for administrator permission to remove the service, udev rule, module-load file, and Speecher-specific group membership it manages. It will not uninstall the distro ydotool package."),
                                             QMessageBox::Cancel | QMessageBox::Ok,
                                             QMessageBox::Cancel);
    if (answer != QMessageBox::Ok) {
        return;
    }
    QString error;
    QString stopError;
    YdotoolSetup::stopUserService(&stopError);
    if (!YdotoolSetup::runHelper(YdotoolSetup::HelperAction::Remove, &error)) {
        QMessageBox::warning(this, QStringLiteral("ydotool removal failed"), error);
        refreshOutputControls();
        return;
    }

    const YdotoolSetupStatus status = YdotoolSetup::probe(false);
    if (status.speecherManagedSetupInstalled) {
        QMessageBox::warning(this,
                             QStringLiteral("ydotool removal incomplete"),
                             QStringLiteral("The privileged helper finished, but Speecher-managed setup files are still detected."));
        refreshOutputControls();
        return;
    }
    m_controller->settings()->setYdotoolEnabled(false);
    QSignalBlocker blocker(m_outputMethod);
    selectData(m_outputMethod, m_controller->settings()->outputMethod());
    refreshOutputControls();
    updateButtonState();
}

bool SettingsDialog::verifyYdotoolTyping()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Verify ydotool"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *label = new QLabel(QStringLiteral("Keep this field focused while Speecher tests virtual keyboard input."), &dialog);
    label->setWordWrap(true);
    auto *field = new QLineEdit(&dialog);
    field->setClearButtonEnabled(true);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto *run = buttons->addButton(QStringLiteral("Run test"), QDialogButtonBox::AcceptRole);
    layout->addWidget(label);
    layout->addWidget(field);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(run, &QPushButton::clicked, &dialog, [field, &dialog] {
        field->clear();
        field->setFocus(Qt::OtherFocusReason);
        QTimer::singleShot(150, field, [field, &dialog] {
            QString error;
            YdotoolDelivery ydotool;
            const QString expected = QStringLiteral("speecher test");
            if (!ydotool.type(expected, &error)) {
                QMessageBox::warning(&dialog, QStringLiteral("ydotool verification failed"), error);
                return;
            }
            QTimer::singleShot(350, field, [field, expected, &dialog] {
                if (field->text() == expected) {
                    dialog.accept();
                } else {
                    QMessageBox::warning(&dialog,
                                         QStringLiteral("ydotool verification failed"),
                                         QStringLiteral("The test field did not receive the expected text."));
                }
            });
        });
    });
    dialog.resize(420, dialog.sizeHint().height());
    field->setFocus(Qt::OtherFocusReason);
    return dialog.exec() == QDialog::Accepted;
}

} // namespace speecher
