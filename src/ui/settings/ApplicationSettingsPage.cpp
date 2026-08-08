#include "ui/settings/ApplicationSettingsPage.h"

#include "ui/settings/SettingsPageSupport.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace speecher {
namespace {

QString categoryLabel(AppCategory category)
{
    switch (category) {
    case AppCategory::General:
        return QStringLiteral("Other app");
    case AppCategory::Terminal:
        return QStringLiteral("Terminal");
    case AppCategory::Browser:
        return QStringLiteral("Browser");
    case AppCategory::Email:
        return QStringLiteral("Email");
    case AppCategory::Office:
        return QStringLiteral("Office");
    case AppCategory::CodeEditor:
        return QStringLiteral("Code editor");
    case AppCategory::AiCoding:
        return QStringLiteral("AI coding");
    case AppCategory::Unknown:
        return QStringLiteral("Automatic");
    }
    return QStringLiteral("Automatic");
}

QString profileLabel(WritingProfile profile)
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
    return QStringLiteral("Automatic");
}

QComboBox *makeCategoryCombo(QWidget *parent)
{
    auto *combo = new QComboBox(parent);
    combo->addItem(QStringLiteral("Automatic"), QString());
    for (const AppCategory category : {AppCategory::General,
                                       AppCategory::Terminal,
                                       AppCategory::Browser,
                                       AppCategory::Email,
                                       AppCategory::Office,
                                       AppCategory::CodeEditor,
                                       AppCategory::AiCoding}) {
        combo->addItem(categoryLabel(category), appCategoryName(category));
    }
    return combo;
}

QComboBox *makeProfileCombo(QWidget *parent)
{
    auto *combo = new QComboBox(parent);
    combo->addItem(QStringLiteral("Automatic"), QString());
    for (const WritingProfile profile : {WritingProfile::Work,
                                         WritingProfile::Email,
                                         WritingProfile::Personal,
                                         WritingProfile::Other}) {
        combo->addItem(profileLabel(profile), writingProfileName(profile));
    }
    return combo;
}

QTableWidgetItem *readOnlyItem(const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    return item;
}

QList<AppRecognitionRule> configuredRules(const AppSettings &settings)
{
    QList<AppRecognitionRule> rules = settings.appRecognitionRules;
    for (const WritingProfileOverride &override : settings.refinement.writingProfileOverrides) {
        if (override.enabled) {
            rules.append({override.applicationId, std::nullopt, override.profile});
        }
    }
    return rules;
}

} // namespace

ApplicationSettingsPage::ApplicationSettingsPage(QWidget *parent)
    : QScrollArea(parent)
    , m_rules(new QTableWidget(this))
    , m_addButton(new QPushButton(QStringLiteral("Add application"), this))
    , m_deleteButton(new QPushButton(QStringLiteral("Delete selected"), this))
{
    m_rules->setObjectName(QStringLiteral("appRecognitionRules"));
    m_addButton->setObjectName(QStringLiteral("addAppRecognitionRule"));
    m_deleteButton->setObjectName(QStringLiteral("deleteAppRecognitionRule"));
    m_rules->setColumnCount(4);
    m_rules->setHorizontalHeaderLabels({
        QStringLiteral("Application ID or name contains"),
        QStringLiteral("App type"),
        QStringLiteral("Writing profile"),
        QStringLiteral("Source"),
    });
    m_rules->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_rules->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_rules->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_rules->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_rules->verticalHeader()->hide();
    m_rules->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_rules->setSelectionMode(QAbstractItemView::SingleSelection);
    m_rules->setMinimumHeight(440);
    m_deleteButton->setEnabled(false);

    auto *title = settings::makePageTitle(QStringLiteral("Applications"), this);
    auto *card = settings::makeSettingsCard(this);
    auto *cardLayout = qobject_cast<QFormLayout *>(card->layout());
    m_controls = new QWidget(card);
    auto *controlsLayout = new QVBoxLayout(m_controls);
    auto *description = new QLabel(
        QStringLiteral("Built-in matches are read-only. Custom matches take priority and can set the app type used for paste rules, the Writing Profile used for refinement, or both."),
        m_controls);
    description->setObjectName(QStringLiteral("rowDescription"));
    description->setWordWrap(true);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(m_deleteButton);
    buttons->addWidget(m_addButton);
    controlsLayout->addWidget(description);
    controlsLayout->addWidget(m_rules);
    controlsLayout->addLayout(buttons);
    cardLayout->addRow(m_controls);

    auto *pageLayout = settings::makeSettingsPage(this);
    pageLayout->setSpacing(0);
    pageLayout->addWidget(title);
    pageLayout->addSpacing(settings::sectionGap());
    pageLayout->addWidget(card);
    pageLayout->addStretch();

    connect(m_rules, &QTableWidget::itemChanged, this, &ApplicationSettingsPage::changed);
    connect(m_rules, &QTableWidget::itemSelectionChanged,
            this, &ApplicationSettingsPage::updateDeleteButton);
    connect(m_addButton, &QPushButton::clicked, this, [this] {
        addCustomRule();
        m_rules->selectRow(m_rules->rowCount() - 1);
        emit changed();
    });
    connect(m_deleteButton, &QPushButton::clicked, this, [this] {
        const int row = m_rules->currentRow();
        if (row >= m_builtInCount) {
            m_rules->removeRow(row);
            updateDeleteButton();
            emit changed();
        }
    });
}

void ApplicationSettingsPage::load(const AppSettings &settings)
{
    const QSignalBlocker blocker(m_rules);
    m_rules->setRowCount(0);
    for (const AppRecognitionRule &rule : builtInAppRecognitionRules()) {
        addBuiltInRule(rule);
    }
    m_builtInCount = m_rules->rowCount();
    for (const AppRecognitionRule &rule : configuredRules(settings)) {
        addCustomRule(rule);
    }
    m_rules->clearSelection();
    updateDeleteButton();
}

void ApplicationSettingsPage::appendToDraft(AppSettings &draft) const
{
    draft.appRecognitionRules = currentRules();
    draft.refinement.writingProfileOverrides.clear();
}

bool ApplicationSettingsPage::hasChanges(const AppSettings &settings) const
{
    return currentRules() != configuredRules(settings);
}

void ApplicationSettingsPage::setTargetAccessibilityAvailable(bool available)
{
    m_controls->setEnabled(available);
    m_controls->setToolTip(
        available
            ? QString()
            : QStringLiteral("Enable desktop accessibility (AT-SPI) to identify target applications."));
}

void ApplicationSettingsPage::addBuiltInRule(const AppRecognitionRule &rule)
{
    const int row = m_rules->rowCount();
    m_rules->insertRow(row);
    m_rules->setItem(row, 0, readOnlyItem(rule.match));
    m_rules->setItem(row, 1, readOnlyItem(rule.category
                                             ? categoryLabel(*rule.category)
                                             : QStringLiteral("Automatic")));
    m_rules->setItem(row, 2, readOnlyItem(rule.writingProfile
                                             ? profileLabel(*rule.writingProfile)
                                             : QStringLiteral("Automatic")));
    m_rules->setItem(row, 3, readOnlyItem(QStringLiteral("Built-in")));
}

void ApplicationSettingsPage::addCustomRule(const AppRecognitionRule &rule)
{
    const int row = m_rules->rowCount();
    m_rules->insertRow(row);
    auto *match = new QTableWidgetItem(rule.match);
    match->setToolTip(QStringLiteral("Matches the application ID, application name, process name, or accessible role."));
    auto *category = makeCategoryCombo(m_rules);
    auto *profile = makeProfileCombo(m_rules);
    if (rule.category) {
        settings::selectData(category, appCategoryName(*rule.category));
    }
    if (rule.writingProfile) {
        settings::selectData(profile, writingProfileName(*rule.writingProfile));
    }
    connect(category, &QComboBox::currentIndexChanged, this, &ApplicationSettingsPage::changed);
    connect(profile, &QComboBox::currentIndexChanged, this, &ApplicationSettingsPage::changed);
    m_rules->setItem(row, 0, match);
    m_rules->setCellWidget(row, 1, category);
    m_rules->setCellWidget(row, 2, profile);
    m_rules->setItem(row, 3, readOnlyItem(QStringLiteral("Custom")));
}

QList<AppRecognitionRule> ApplicationSettingsPage::currentRules() const
{
    QList<AppRecognitionRule> rules;
    for (int row = m_builtInCount; row < m_rules->rowCount(); ++row) {
        const QTableWidgetItem *matchItem = m_rules->item(row, 0);
        const auto *category = qobject_cast<QComboBox *>(m_rules->cellWidget(row, 1));
        const auto *profile = qobject_cast<QComboBox *>(m_rules->cellWidget(row, 2));
        if (!matchItem || !category || !profile) {
            continue;
        }
        AppRecognitionRule rule;
        rule.match = matchItem->text().trimmed();
        const QString categoryName = category->currentData().toString();
        const QString profileName = profile->currentData().toString();
        if (!categoryName.isEmpty()) {
            rule.category = appCategoryFromName(categoryName);
        }
        if (!profileName.isEmpty()) {
            rule.writingProfile = writingProfileFromName(profileName);
        }
        if (!rule.match.isEmpty() && (rule.category || rule.writingProfile)) {
            rules.append(rule);
        }
    }
    return rules;
}

void ApplicationSettingsPage::updateDeleteButton()
{
    m_deleteButton->setEnabled(m_rules->currentRow() >= m_builtInCount);
}

} // namespace speecher
