#include "ui/settings/RefinementSettingsPage.h"

#include "providers/ProviderRegistry.h"
#include "ui/settings/SettingsPageSupport.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace speecher {

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

RefinementSettingsPage::RefinementSettingsPage(ProviderRegistry &providers, QWidget *parent)
    : QScrollArea(parent)
    , m_provider(new QComboBox(this))
    , m_writingProfile(new QComboBox(this))
    , m_useTargetContext(new QCheckBox(this))
    , m_screenshotContext(new QCheckBox(this))
    , m_profileSettings(new QTableWidget(this))
    , m_appProfileOverrides(new QTableWidget(this))
{
    for (const ProviderDescriptor &provider : providers.refinementProviders()) {
        m_provider->addItem(provider.label, provider.id);
    }
    m_provider->addItem(QStringLiteral("None"), QStringLiteral("none"));
    addWritingProfiles(m_writingProfile);
    m_useTargetContext->setText(QStringLiteral("Use context"));
    m_screenshotContext->setText(QStringLiteral("Allow screenshots"));
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

    auto *section = settings::makeSectionLabel(QStringLiteral("Refinement"), this);
    auto *card = settings::makeSettingsCard(this);
    auto *cardLayout = qobject_cast<QVBoxLayout *>(card->layout());
    settings::addRow(cardLayout, settings::makeRow(QStringLiteral("Refinement"), QStringLiteral("Clean up dictated text after capture."), m_provider, card), card);
    settings::addRow(cardLayout, settings::makeRow(QStringLiteral("Fallback profile"), QStringLiteral("Writing profile used when the target app does not imply one."), m_writingProfile, card), card);
    auto *profileSettingsControl = new QWidget(card);
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
    cardLayout->addWidget(profileSettingsControl);
    cardLayout->addWidget(settings::makeSeparator(card));

    m_profileOverridesControl = new QWidget(card);
    m_profileOverridesControl->setObjectName(QStringLiteral("applicationProfileOverrides"));
    auto *profileOverridesLayout = new QVBoxLayout(m_profileOverridesControl);
    auto *profileOverridesTitle = new QLabel(QStringLiteral("App-specific profile overrides"), m_profileOverridesControl);
    profileOverridesTitle->setObjectName(QStringLiteral("subsectionLabel"));
    auto *profileOverridesDescription = new QLabel(
        QStringLiteral("An exact application ID overrides automatic category detection and the fallback profile."),
        m_profileOverridesControl);
    profileOverridesDescription->setObjectName(QStringLiteral("rowDescription"));
    profileOverridesDescription->setWordWrap(true);
    m_addAppProfileOverrideButton = new QPushButton(QStringLiteral("Add override"), m_profileOverridesControl);
    m_removeAppProfileOverrideButton = new QPushButton(QStringLiteral("Delete selected"), m_profileOverridesControl);
    m_removeAppProfileOverrideButton->setEnabled(false);
    auto *profileOverrideButtons = new QHBoxLayout;
    profileOverrideButtons->addStretch();
    profileOverrideButtons->addWidget(m_removeAppProfileOverrideButton);
    profileOverrideButtons->addWidget(m_addAppProfileOverrideButton);
    profileOverridesLayout->addWidget(profileOverridesTitle);
    profileOverridesLayout->addWidget(profileOverridesDescription);
    profileOverridesLayout->addWidget(m_appProfileOverrides);
    profileOverridesLayout->addLayout(profileOverrideButtons);
    cardLayout->addWidget(m_profileOverridesControl);
    cardLayout->addWidget(settings::makeSeparator(card));

    m_targetContextControl = settings::makeRow(
        QStringLiteral("Target context"),
        QStringLiteral("Use the focused app, control, selection, and bounded nearby text for cleanup."),
        m_useTargetContext,
        card);
    m_targetContextControl->setObjectName(QStringLiteral("targetContextControl"));
    settings::addRow(cardLayout, m_targetContextControl, card);
    settings::addRow(cardLayout, settings::makeRow(QStringLiteral("Screenshot context"), QStringLiteral("Off by default; used only with image-capable refinement models."), m_screenshotContext, card), card, false);

    auto *pageLayout = settings::makeSettingsPage(this);
    pageLayout->addWidget(section);
    pageLayout->addWidget(card);
    pageLayout->addStretch();

    connect(m_provider, &QComboBox::currentIndexChanged, this, [this] {
        updateScreenshotControl();
        emit changed();
    });
    connect(m_writingProfile, &QComboBox::currentIndexChanged, this, &RefinementSettingsPage::changed);
    connect(m_appProfileOverrides, &QTableWidget::itemChanged, this, &RefinementSettingsPage::changed);
    connect(m_appProfileOverrides, &QTableWidget::itemSelectionChanged, this, [this] {
        m_removeAppProfileOverrideButton->setEnabled(m_appProfileOverrides->currentRow() >= 0);
    });
    connect(m_addAppProfileOverrideButton, &QPushButton::clicked, this, [this] {
        addWritingProfileOverride();
        emit changed();
    });
    connect(m_removeAppProfileOverrideButton, &QPushButton::clicked, this, [this] {
        const int row = m_appProfileOverrides->currentRow();
        if (row >= 0) {
            m_appProfileOverrides->removeRow(row);
            emit changed();
        }
    });
    connect(m_useTargetContext, &QCheckBox::toggled, this, &RefinementSettingsPage::changed);
    connect(m_screenshotContext, &QCheckBox::toggled, this, &RefinementSettingsPage::changed);
}

void RefinementSettingsPage::setTargetAccessibilityAvailable(bool available)
{
    const QString explanation = available
        ? QString()
        : QStringLiteral("Enable desktop accessibility (AT-SPI) to use target-aware refinement.");
    m_profileOverridesControl->setEnabled(available);
    m_profileOverridesControl->setToolTip(explanation);
    m_targetContextControl->setEnabled(available);
    m_targetContextControl->setToolTip(explanation);
}

void RefinementSettingsPage::load(const AppSettings &settings)
{
    settings::selectData(m_provider, settings.refinement.providerId);
    settings::selectData(m_writingProfile, settings.refinement.defaultWritingProfile);
    setWritingProfileSettings(settings.refinement.writingProfiles);
    setWritingProfileOverrides(settings.refinement.writingProfileOverrides);
    m_useTargetContext->setChecked(settings.refinement.useTargetContext);
    m_screenshotContext->setChecked(settings.refinement.includeScreenshotContext);
    updateScreenshotControl();
}

bool RefinementSettingsPage::validate() const
{
    QSet<QString> applicationIds;
    for (const WritingProfileOverride &override : currentWritingProfileOverrides()) {
        const QString id = override.applicationId.toCaseFolded();
        if (applicationIds.contains(id)) {
            QMessageBox::warning(const_cast<RefinementSettingsPage *>(this),
                                 QStringLiteral("Writing profiles not saved"),
                                 QStringLiteral("Each application ID can have only one Writing Profile override."));
            return false;
        }
        applicationIds.insert(id);
    }
    return true;
}

void RefinementSettingsPage::appendToDraft(AppSettings &draft) const
{
    draft.refinement.providerId = m_provider->currentData().toString();
    draft.refinement.defaultWritingProfile = m_writingProfile->currentData().toString();
    draft.refinement.writingProfiles = currentWritingProfileSettings();
    draft.refinement.writingProfileOverrides = currentWritingProfileOverrides();
    draft.refinement.useTargetContext = m_useTargetContext->isChecked();
    draft.refinement.includeScreenshotContext = m_screenshotContext->isChecked();
}

bool RefinementSettingsPage::hasChanges(const AppSettings &settings) const
{
    AppSettings draft = settings;
    appendToDraft(draft);
    return draft.refinement.providerId != settings.refinement.providerId
        || draft.refinement.defaultWritingProfile != settings.refinement.defaultWritingProfile
        || draft.refinement.writingProfiles != settings.refinement.writingProfiles
        || draft.refinement.writingProfileOverrides != settings.refinement.writingProfileOverrides
        || draft.refinement.useTargetContext != settings.refinement.useTargetContext
        || draft.refinement.includeScreenshotContext != settings.refinement.includeScreenshotContext;
}

QList<WritingProfileSettings> RefinementSettingsPage::currentWritingProfileSettings() const
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

QList<WritingProfileOverride> RefinementSettingsPage::currentWritingProfileOverrides() const
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

void RefinementSettingsPage::setWritingProfileSettings(const QList<WritingProfileSettings> &settings)
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
        settings::selectData(strength, profileSettings.cleanupStrength);
        auto *tone = new QComboBox(m_profileSettings);
        addWritingTones(tone);
        settings::selectData(tone, profileSettings.tone);
        connect(strength, &QComboBox::currentIndexChanged, this, &RefinementSettingsPage::changed);
        connect(tone, &QComboBox::currentIndexChanged, this, &RefinementSettingsPage::changed);
        m_profileSettings->setItem(row, 0, profile);
        m_profileSettings->setCellWidget(row, 1, strength);
        m_profileSettings->setCellWidget(row, 2, tone);
    }
}

void RefinementSettingsPage::setWritingProfileOverrides(const QList<WritingProfileOverride> &overrides)
{
    QSignalBlocker blocker(m_appProfileOverrides);
    m_appProfileOverrides->setRowCount(0);
    for (const WritingProfileOverride &override : overrides) {
        addWritingProfileOverride(override);
    }
    m_removeAppProfileOverrideButton->setEnabled(false);
}

void RefinementSettingsPage::addWritingProfileOverride(const WritingProfileOverride &override)
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
    settings::selectData(profile, writingProfileName(override.profile));
    connect(profile, &QComboBox::currentIndexChanged, this, &RefinementSettingsPage::changed);
    m_appProfileOverrides->setItem(row, 0, enabled);
    m_appProfileOverrides->setItem(row, 1, application);
    m_appProfileOverrides->setCellWidget(row, 2, profile);
    if (override.applicationId.isEmpty()) {
        m_appProfileOverrides->setCurrentCell(row, 1);
        m_appProfileOverrides->editItem(application);
    }
}

void RefinementSettingsPage::updateScreenshotControl()
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

} // namespace speecher
