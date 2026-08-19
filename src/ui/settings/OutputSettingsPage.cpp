#include "ui/settings/OutputSettingsPage.h"

#include "core/OutputMethod.h"
#include "core/SettingsStore.h"
#ifdef SPEECHER_WITH_YDOTOOL
#include "output/YdotoolSetup.h"
#include "output/YdotoolSetupFlow.h"
#endif
#include "ui/settings/SettingsPageSupport.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QSet>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolTip>
#include <QThread>
#include <QVBoxLayout>

#include <utility>

namespace speecher {

namespace {

void runSetupWork(QObject *context,
                  std::function<void()> work,
                  std::function<void()> finished)
{
    QThread *thread = QThread::create(std::move(work));
    QObject::connect(thread, &QThread::finished, context, std::move(finished));
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

} // namespace

static void updateWrappedHeight(QLabel *label, int width)
{
    label->setMinimumHeight(0);
    label->setMinimumHeight(label->heightForWidth(width));
}

class WrappedStatusLabel final : public QLabel {
public:
    using QLabel::QLabel;

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        updateWrappedHeight(this, event->size().width());
    }
};

#ifdef SPEECHER_WITH_YDOTOOL
static void setWrappedText(QLabel *label, const QString &text)
{
    label->setText(text);
    updateWrappedHeight(label, label->width());
}
#endif

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
#ifdef Q_OS_MACOS
    // Terminals on macOS take the same Cmd+V as every other app, so the two
    // paste chords name the same keystroke here.
    combo->addItem(QStringLiteral("Standard paste (Cmd+V)"), pasteMethodName(PasteMethod::StandardPaste));
    combo->addItem(QStringLiteral("Terminal paste (Cmd+V)"), pasteMethodName(PasteMethod::TerminalPaste));
    if (includeDirectInsert) {
        combo->addItem(QStringLiteral("Direct insertion (Accessibility)"), pasteMethodName(PasteMethod::DirectInsert));
    }
#else
    combo->addItem(QStringLiteral("Standard paste (Ctrl+V)"), pasteMethodName(PasteMethod::StandardPaste));
    combo->addItem(QStringLiteral("Terminal paste (Ctrl+Shift+V)"), pasteMethodName(PasteMethod::TerminalPaste));
    if (includeDirectInsert) {
        combo->addItem(QStringLiteral("Direct insertion (AT-SPI)"), pasteMethodName(PasteMethod::DirectInsert));
    }
#endif
    combo->addItem(QStringLiteral("Clipboard only"), pasteMethodName(PasteMethod::ClipboardOnly));
}

static QString targetAccessibilityHint()
{
#ifdef Q_OS_MACOS
    return QStringLiteral("Grant Accessibility permission so Speecher can identify the target application.");
#else
    return QStringLiteral("Enable desktop accessibility (AT-SPI) to identify the target application.");
#endif
}

static QString applicationIdHint()
{
#ifdef Q_OS_MACOS
    return QStringLiteral("Use the app's bundle identifier, such as com.apple.Terminal.");
#else
    return QStringLiteral("Use the desktop application ID reported by AT-SPI.");
#endif
}

static QList<AppCategory> managedPasteCategories()
{
    return {
        AppCategory::Terminal,
        AppCategory::Browser,
        AppCategory::Email,
        AppCategory::Office,
        AppCategory::CodeEditor,
        AppCategory::AiCoding,
        AppCategory::General,
    };
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

OutputSettingsPage::OutputSettingsPage(SettingsStore &settings, QWidget *parent)
    : QScrollArea(parent)
    , m_settings(settings)
    , m_outputMethod(new QComboBox(this))
    , m_outputFormat(new QComboBox(this))
    , m_globalPaste(new QComboBox(this))
    , m_restoreClipboardAfterTyping(new QCheckBox(this))
    , m_completionStatusDuration(new QSpinBox(this))
    , m_ydotoolStatus(new WrappedStatusLabel(this))
    , m_appPasteRules(new QTableWidget(this))
    , m_addAppPasteRuleButton(new QPushButton(QStringLiteral("Add rule"), this))
    , m_removeAppPasteRuleButton(new QPushButton(QStringLiteral("Delete selected"), this))
    , m_ydotoolSetupButton(new QPushButton(QStringLiteral("Set up"), this))
    , m_ydotoolStartButton(new QPushButton(QStringLiteral("Start service"), this))
    , m_ydotoolDisableButton(new QPushButton(QStringLiteral("Disable in Speecher"), this))
    , m_ydotoolRemoveButton(new QPushButton(QStringLiteral("Remove setup"), this))
{
    const auto addOutputMethod = [this](const char *method) {
        m_outputMethod->addItem(OutputMethod::label(QString::fromLatin1(method)), QString::fromLatin1(method));
    };
    addOutputMethod(OutputMethod::Automatic);
#ifdef Q_OS_MACOS
    addOutputMethod(OutputMethod::MacPaste);
#else
    addOutputMethod(OutputMethod::Ydotool);
    addOutputMethod(OutputMethod::WlCopy);
#endif
    addOutputMethod(OutputMethod::QtClipboard);
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
#ifdef Q_OS_MACOS
    m_restoreClipboardAfterTyping->setToolTip(QStringLiteral("Restore the previous clipboard after Speecher pastes."));
#else
    m_restoreClipboardAfterTyping->setToolTip(QStringLiteral("Restore the previous clipboard after virtual-keyboard paste."));
#endif
    m_completionStatusDuration->setObjectName(QStringLiteral("completionStatusDuration"));
    m_completionStatusDuration->setRange(0, 5000);
    m_completionStatusDuration->setSingleStep(50);
    m_completionStatusDuration->setSuffix(QStringLiteral(" ms"));
    m_appPasteRules->setObjectName(QStringLiteral("vocabInput"));
    m_appPasteRules->setColumnCount(3);
    m_appPasteRules->setHorizontalHeaderLabels({QStringLiteral("Enabled"), QStringLiteral("Application ID"), QStringLiteral("Paste behavior")});
    m_appPasteRules->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_appPasteRules->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_appPasteRules->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_appPasteRules->verticalHeader()->hide();
    m_appPasteRules->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_appPasteRules->setSelectionMode(QAbstractItemView::SingleSelection);
    m_appPasteRules->setMinimumHeight(150);
    for (QPushButton *button : {m_ydotoolSetupButton, m_ydotoolStartButton, m_ydotoolDisableButton, m_ydotoolRemoveButton}) {
        button->setMinimumWidth(button->fontMetrics().horizontalAdvance(button->text()) + 36);
        button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    }
#ifndef SPEECHER_WITH_YDOTOOL
    updateYdotoolButtons();
#endif

    auto *title = settings::makePageTitle(QStringLiteral("Output"), this);
    auto *card = settings::makeSettingsCard(this);
    auto *cardLayout = settings::cardFormLayout(card);
    settings::addSectionRow(cardLayout, QStringLiteral("Delivery"), card);
    settings::addRow(cardLayout, settings::makeRow(QStringLiteral("Method"), QStringLiteral("How Speecher delivers final text."), m_outputMethod, card), card);
    settings::addRow(cardLayout, settings::makeRow(QStringLiteral("Format"), QStringLiteral("Default clipboard representation. A CLI shortcut can override this per dictation."), m_outputFormat, card), card);
    settings::addRow(cardLayout, settings::makeRow(QStringLiteral("Status duration"), QStringLiteral("How long the completed delivery result stays visible."), m_completionStatusDuration, card), card);
    settings::addSectionRow(cardLayout, QStringLiteral("Paste behavior"), card);
    settings::addRow(cardLayout, settings::makeRow(QStringLiteral("Global fallback"), QStringLiteral("Paste behavior used unless a category or exact-app rule overrides it."), m_globalPaste, card), card);
    m_targetPasteControls = new QWidget(card);
    m_targetPasteControls->setObjectName(QStringLiteral("targetPasteControls"));
    auto *targetPasteLayout = new QFormLayout(m_targetPasteControls);
    targetPasteLayout->setContentsMargins(0, 0, 0, 0);
    settings::configureFormLayout(targetPasteLayout);
    const QHash<AppCategory, QString> categoryLabels{
        {AppCategory::Terminal, QStringLiteral("Terminals")},
        {AppCategory::Browser, QStringLiteral("Browsers")},
        {AppCategory::Email, QStringLiteral("Email apps")},
        {AppCategory::Office, QStringLiteral("Office apps")},
        {AppCategory::CodeEditor, QStringLiteral("Code editors")},
        {AppCategory::AiCoding, QStringLiteral("AI coding apps")},
        {AppCategory::General, QStringLiteral("Other apps")},
    };
    for (const auto &[category, combo] : std::as_const(m_categoryPasteControls)) {
        settings::addRow(targetPasteLayout,
                         settings::makeRow(categoryLabels.value(category),
                                           QStringLiteral("Override the fallback for this application category."),
                                           combo,
                                           m_targetPasteControls),
                         m_targetPasteControls);
    }
    auto *appRulesControl = new QWidget(m_targetPasteControls);
    auto *appRulesLayout = new QVBoxLayout(appRulesControl);
    auto *appRulesTitle = new QLabel(QStringLiteral("App-specific paste rules"), appRulesControl);
    appRulesTitle->setObjectName(QStringLiteral("subsectionLabel"));
    auto *appRulesDescription = new QLabel(
#ifdef Q_OS_MACOS
        QStringLiteral("Override paste behavior for an exact bundle identifier, such as com.apple.Terminal."),
#else
        QStringLiteral("Override paste behavior for an exact application ID, such as org.kde.konsole."),
#endif
        appRulesControl);
    appRulesDescription->setObjectName(QStringLiteral("rowDescription"));
    appRulesDescription->setWordWrap(true);
    m_removeAppPasteRuleButton->setEnabled(false);
    auto *appRuleButtons = new QHBoxLayout;
    appRuleButtons->addStretch();
    appRuleButtons->addWidget(m_removeAppPasteRuleButton);
    appRuleButtons->addWidget(m_addAppPasteRuleButton);
    appRulesLayout->addWidget(appRulesTitle);
    appRulesLayout->addWidget(appRulesDescription);
    appRulesLayout->addWidget(m_appPasteRules);
    appRulesLayout->addLayout(appRuleButtons);
    // Keep spanning rows from stretching the form to the card edge, which
    // defeats the centered-form alignment every other page shares.
    m_appPasteRules->setMaximumWidth(640);
    appRulesControl->setMaximumWidth(640);
    targetPasteLayout->addRow(appRulesControl);
    cardLayout->addRow(m_targetPasteControls);
    settings::addSectionRow(cardLayout, QStringLiteral("Clipboard && virtual keyboard"), card);
    settings::addRow(cardLayout, settings::makeRow(QStringLiteral("Clipboard"), QStringLiteral("Restore previous clipboard contents after typing"), m_restoreClipboardAfterTyping, card), card);
    settings::addRow(cardLayout, settings::makeRow(QStringLiteral("Virtual keyboard"), QString(), makeYdotoolControl(m_ydotoolStatus, m_ydotoolSetupButton, m_ydotoolStartButton, m_ydotoolDisableButton, m_ydotoolRemoveButton, card), card), card, false);
    auto *pageLayout = settings::makeSettingsPage(this);
    pageLayout->setSpacing(0);
    pageLayout->addWidget(title);
    pageLayout->addSpacing(settings::sectionGap());
    pageLayout->addWidget(card);
    pageLayout->addStretch();

    connect(m_restoreClipboardAfterTyping, &QCheckBox::toggled, this, &OutputSettingsPage::changed);
    connect(m_completionStatusDuration, &QSpinBox::valueChanged, this, &OutputSettingsPage::changed);
    connect(m_outputFormat, &QComboBox::currentIndexChanged, this, &OutputSettingsPage::changed);
    connect(m_globalPaste, &QComboBox::currentIndexChanged, this, &OutputSettingsPage::changed);
    for (const auto &[category, combo] : std::as_const(m_categoryPasteControls)) {
        Q_UNUSED(category);
        connect(combo, &QComboBox::currentIndexChanged, this, &OutputSettingsPage::changed);
    }
    connect(m_appPasteRules, &QTableWidget::itemChanged, this, &OutputSettingsPage::changed);
    connect(m_appPasteRules, &QTableWidget::itemSelectionChanged, this, [this] { m_removeAppPasteRuleButton->setEnabled(m_appPasteRules->currentRow() >= 0); });
    connect(m_addAppPasteRuleButton, &QPushButton::clicked, this, [this] { addApplicationPasteRule(); emit changed(); });
    connect(m_removeAppPasteRuleButton, &QPushButton::clicked, this, [this] {
        const int row = m_appPasteRules->currentRow();
        if (row >= 0) { m_appPasteRules->removeRow(row); emit changed(); }
    });
    connect(m_outputMethod, &QComboBox::currentIndexChanged, this, [this] {
        // The user has now chosen for themselves, so there is nothing left to
        // preserve.
        m_unlistedOutputMethod.clear();
#ifdef SPEECHER_WITH_YDOTOOL
        if (m_outputMethod->currentData().toString() == QString::fromLatin1(OutputMethod::Ydotool)) {
            const YdotoolSetupStatus status = YdotoolSetup::probe(m_settings.ydotoolEnabled());
            if (!status.ready() || !m_settings.ydotoolEnabled()) {
                QSignalBlocker blocker(m_outputMethod);
                settings::selectData(m_outputMethod, m_settings.outputMethod());
                QToolTip::showText(m_outputMethod->mapToGlobal(m_outputMethod->rect().bottomLeft()), QStringLiteral("Set up ydotool first"), m_outputMethod);
                return;
            }
        }
#endif
        emit changed();
    });
#ifdef SPEECHER_WITH_YDOTOOL
    connect(m_ydotoolSetupButton, &QPushButton::clicked, this, &OutputSettingsPage::setupOrEnableYdotool);
    connect(m_ydotoolStartButton, &QPushButton::clicked, this, [this] {
        m_ydotoolStartButton->setEnabled(false);
        const auto error = std::make_shared<QString>();
        runSetupWork(
            this,
            [error] { YdotoolSetup::startUserService(error.get()); },
            [this, error] {
                if (!error->isEmpty()) {
                    QMessageBox::warning(this, QStringLiteral("ydotool service"), *error);
                }
                refreshControls();
            });
    });
    connect(m_ydotoolDisableButton, &QPushButton::clicked, this, &OutputSettingsPage::disableYdotool);
    connect(m_ydotoolRemoveButton, &QPushButton::clicked, this, &OutputSettingsPage::removeYdotoolSetup);
#endif
}

void OutputSettingsPage::setTargetAccessibilityAvailable(bool available)
{
    m_targetPasteControls->setEnabled(available);
    m_targetPasteControls->setToolTip(available ? QString() : targetAccessibilityHint());
}

void OutputSettingsPage::load(const AppSettings &settings)
{
    // selectData falls back to index 0, which is Automatic on every platform.
    // Remember what it could not show so appendToDraft does not quietly rewrite
    // a method this build simply has no item for.
    const bool listed = m_outputMethod->findData(settings.output.method) >= 0;
    settings::selectData(m_outputMethod, settings.output.method);
    m_unlistedOutputMethod = listed ? QString() : settings.output.method;
    settings::selectData(m_outputFormat, outputFormatName(settings.output.format));
    const QList<PasteRule> &pasteRules = settings.output.pasteRules;
    settings::selectData(m_globalPaste, pasteMethodName(pasteMethodFor(pasteRules, PasteRuleScope::Global, QString(), PasteMethod::StandardPaste)));
    for (const auto &[category, combo] : std::as_const(m_categoryPasteControls)) {
        QString method = QStringLiteral("inherit");
        for (const PasteRule &rule : pasteRules) {
            if (rule.scope == PasteRuleScope::Category && rule.match == appCategoryName(category)) { method = pasteMethodName(rule.method); break; }
        }
        settings::selectData(combo, method);
    }
    setApplicationPasteRules(pasteRules);
    m_restoreClipboardAfterTyping->setChecked(settings.output.restoreClipboardAfterTyping);
    m_completionStatusDuration->setValue(settings.output.completionStatusDurationMs);
}

QStringList OutputSettingsPage::validate(bool showError) const
{
    const QStringList messages = validatePasteRules(currentApplicationPasteRules());
    if (!messages.isEmpty() && showError) {
        QMessageBox::warning(const_cast<OutputSettingsPage *>(this),
                             QStringLiteral("Paste rules not saved"),
                             messages.join(QLatin1Char('\n')));
    }
    return messages;
}

void OutputSettingsPage::appendToDraft(AppSettings &draft) const
{
    draft.output.method = m_unlistedOutputMethod.isEmpty()
        ? m_outputMethod->currentData().toString()
        : m_unlistedOutputMethod;
    draft.output.format = outputFormatFromString(m_outputFormat->currentData().toString());
    draft.output.pasteRules = withPasteRules(draft.output.pasteRules, currentApplicationPasteRules(), currentCategoryPasteRules(), pasteMethodFromName(m_globalPaste->currentData().toString()));
    draft.output.restoreClipboardAfterTyping = m_restoreClipboardAfterTyping->isChecked();
    draft.output.completionStatusDurationMs = m_completionStatusDuration->value();
}

bool OutputSettingsPage::hasChanges(const AppSettings &settings) const
{
    AppSettings draft = settings;
    appendToDraft(draft);
    return draft.output.method != settings.output.method
        || draft.output.format != settings.output.format
        || draft.output.pasteRules != settings.output.pasteRules
        || draft.output.restoreClipboardAfterTyping != settings.output.restoreClipboardAfterTyping
        || draft.output.completionStatusDurationMs != settings.output.completionStatusDurationMs;
}

QList<PasteRule> OutputSettingsPage::currentApplicationPasteRules() const
{
    QList<PasteRule> rules;
    for (int row = 0; row < m_appPasteRules->rowCount(); ++row) {
        const QTableWidgetItem *enabled = m_appPasteRules->item(row, 0);
        const QTableWidgetItem *application = m_appPasteRules->item(row, 1);
        const auto *method = qobject_cast<QComboBox *>(m_appPasteRules->cellWidget(row, 2));
        const QString applicationId = application ? application->text().trimmed() : QString();
        if (applicationId.isEmpty() || !method) continue;
        rules.append({PasteRuleScope::Application, applicationId, pasteMethodFromName(method->currentData().toString()), enabled && enabled->checkState() == Qt::Checked});
    }
    return rules;
}

QList<PasteRule> OutputSettingsPage::currentCategoryPasteRules() const
{
    QList<PasteRule> rules;
    for (const auto &[category, combo] : m_categoryPasteControls) {
        if (combo->currentData().toString() == QStringLiteral("inherit")) continue;
        rules.append({PasteRuleScope::Category, appCategoryName(category), pasteMethodFromName(combo->currentData().toString()), true});
    }
    return rules;
}

void OutputSettingsPage::setApplicationPasteRules(const QList<PasteRule> &rules)
{
    QSignalBlocker blocker(m_appPasteRules);
    m_appPasteRules->setRowCount(0);
    for (const PasteRule &rule : rules) if (rule.scope == PasteRuleScope::Application) addApplicationPasteRule(rule);
    m_removeAppPasteRuleButton->setEnabled(false);
}

void OutputSettingsPage::addApplicationPasteRule(const PasteRule &rule)
{
    const int row = m_appPasteRules->rowCount();
    m_appPasteRules->insertRow(row);
    auto *enabled = new QTableWidgetItem;
    enabled->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    enabled->setCheckState(rule.enabled ? Qt::Checked : Qt::Unchecked);
    auto *application = new QTableWidgetItem(rule.match);
    application->setToolTip(applicationIdHint());
    auto *method = new QComboBox(m_appPasteRules);
    addPasteMethods(method, true);
    settings::selectData(method, pasteMethodName(rule.method));
    connect(method, &QComboBox::currentIndexChanged, this, &OutputSettingsPage::changed);
    m_appPasteRules->setItem(row, 0, enabled);
    m_appPasteRules->setItem(row, 1, application);
    m_appPasteRules->setCellWidget(row, 2, method);
    if (rule.match.isEmpty()) { m_appPasteRules->setCurrentCell(row, 1); m_appPasteRules->editItem(application); }
}

#ifdef SPEECHER_WITH_YDOTOOL
void OutputSettingsPage::refreshControls()
{
    const YdotoolSetupStatus status = YdotoolSetup::probe(m_settings.ydotoolEnabled());
    const bool ydotoolEnabled = m_settings.ydotoolEnabled() && status.ready();
    const int ydotoolIndex = m_outputMethod->findData(QString::fromLatin1(OutputMethod::Ydotool));
    settings::setComboItemEnabled(m_outputMethod, ydotoolIndex, ydotoolEnabled, ydotoolEnabled ? QString() : QStringLiteral("Set up ydotool first"));
    const bool unavailableSelection = !ydotoolEnabled
        && m_outputMethod->currentData().toString() == QString::fromLatin1(OutputMethod::Ydotool);
    m_outputMethod->setToolTip(
        unavailableSelection
            ? QStringLiteral("Type with ydotool paste is selected but unavailable until virtual keyboard setup passes.")
            : ydotoolEnabled
                ? QStringLiteral("Automatic tries ydotool paste, wl-copy, then Qt clipboard.")
                : QStringLiteral("Type with ydotool paste is disabled until virtual keyboard setup passes."));
    setWrappedText(m_ydotoolStatus, status.label + QStringLiteral(". ") + status.detail);
    updateYdotoolButtons();
}

void OutputSettingsPage::updateYdotoolButtons()
{
    const YdotoolSetupStatus status = YdotoolSetup::probe(m_settings.ydotoolEnabled());
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
    m_ydotoolDisableButton->setVisible(ready && m_settings.ydotoolEnabled());
    m_ydotoolRemoveButton->setVisible(status.speecherManagedSetupInstalled);
    m_ydotoolRemoveButton->setEnabled(status.speecherManagedSetupInstalled);
    m_ydotoolRemoveButton->setToolTip(setupInstalled ? QString() : setupFirst);
}

void OutputSettingsPage::setupOrEnableYdotool()
{
    m_ydotoolSetupButton->setEnabled(false);
    if (!startYdotoolSetup(
            m_settings,
            this,
            YdotoolSetupFlowOptions{
                .confirmInstall = true,
                .applyAutomaticOutputMethod = false,
            },
            this,
            [this](const YdotoolSetupFlowResult &result) {
                if (!result.helperOk) {
                    QMessageBox::warning(
                        this,
                        QStringLiteral("ydotool setup failed"),
                        result.helperError);
                } else if (!result.serviceError.isEmpty()) {
                    QMessageBox::warning(
                        this,
                        QStringLiteral("ydotool service"),
                        result.serviceError);
                }
                refreshControls();
                emit changed();
            })) {
        refreshControls();
    }
}

void OutputSettingsPage::disableYdotool()
{
    m_settings.setYdotoolEnabled(false);
    QSignalBlocker blocker(m_outputMethod);
    settings::selectData(m_outputMethod, m_settings.outputMethod());
    refreshControls();
    emit changed();
}

void OutputSettingsPage::removeYdotoolSetup()
{
    const int answer = QMessageBox::question(this, QStringLiteral("Remove virtual keyboard setup"), QStringLiteral("Speecher will ask for administrator permission to remove the service, udev rule, module-load file, and Speecher-specific group membership it manages. It will not uninstall the distro ydotool package."), QMessageBox::Cancel | QMessageBox::Ok, QMessageBox::Cancel);
    if (answer != QMessageBox::Ok) return;
    m_ydotoolRemoveButton->setEnabled(false);
    struct RemovalResult {
        bool helperOk = false;
        QString helperError;
        QString stopError;
        YdotoolSetupStatus status;
    };
    const auto result = std::make_shared<RemovalResult>();
    runSetupWork(
        this,
        [result] {
            YdotoolSetup::stopUserService(&result->stopError);
            result->helperOk = YdotoolSetup::runHelper(
                YdotoolSetup::HelperAction::Remove, &result->helperError);
            result->status = YdotoolSetup::probe(false);
        },
        [this, result] {
            if (!result->helperOk) {
                QMessageBox::warning(
                    this, QStringLiteral("ydotool removal failed"), result->helperError);
            } else if (result->status.speecherManagedSetupInstalled) {
                QMessageBox::warning(
                    this,
                    QStringLiteral("ydotool removal incomplete"),
                    QStringLiteral("The privileged helper finished, but Speecher-managed setup files are still detected."));
            } else {
                m_settings.setYdotoolEnabled(false);
                QSignalBlocker blocker(m_outputMethod);
                settings::selectData(m_outputMethod, m_settings.outputMethod());
                emit changed();
            }
            refreshControls();
        });
}
#else
// ydotool is Linux-only, and so is the Advanced card that hosted its controls.
// The widgets are still members, so keep them out of the page.
void OutputSettingsPage::refreshControls()
{
#ifdef Q_OS_MACOS
    m_outputMethod->setToolTip(
        QStringLiteral("Automatic pastes with Cmd+V, then falls back to the clipboard."));
#endif
    updateYdotoolButtons();
}

void OutputSettingsPage::updateYdotoolButtons()
{
    m_ydotoolStatus->setVisible(false);
    for (QPushButton *button : {m_ydotoolSetupButton, m_ydotoolStartButton, m_ydotoolDisableButton, m_ydotoolRemoveButton}) {
        button->setVisible(false);
    }
}

void OutputSettingsPage::setupOrEnableYdotool() {}

void OutputSettingsPage::disableYdotool() {}

void OutputSettingsPage::removeYdotoolSetup() {}
#endif

} // namespace speecher
