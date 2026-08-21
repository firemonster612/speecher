#include "ui/settings/RefinementSettingsPage.h"

#include "providers/ProviderRegistry.h"
#include "ui/settings/SettingsPageSupport.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
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
    combo->addItem(QStringLiteral("AI coding"), QStringLiteral("ai_coding"));
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
    case WritingProfile::AiCoding:
        return QStringLiteral("AI coding");
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
{
    for (const ProviderDescriptor &provider : providers.refinementProviders()) {
        m_provider->addItem(provider.label, provider.id);
    }
    m_provider->addItem(QStringLiteral("None"), QStringLiteral("none"));
    addWritingProfiles(m_writingProfile);
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
    m_profileSettings->setMinimumHeight(207);
    m_profileSettings->setMaximumHeight(207);
    // Keep spanning rows from stretching the form to the card edge, which
    // defeats the centered-form alignment every other page shares.
    m_profileSettings->setMaximumWidth(600);
    auto *title = settings::makePageTitle(QStringLiteral("Refinement"), this);
    auto *card = settings::makeSettingsCard(this);
    auto *cardLayout = settings::cardFormLayout(card);
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
    profileSettingsControl->setMaximumWidth(600);
    cardLayout->addWidget(profileSettingsControl);
    cardLayout->addRow(settings::makeCenteredSeparator(card));

    m_targetContextControl = settings::makeRow(
        QStringLiteral("Context"),
        QStringLiteral("Send the target app's context to the refiner"),
        m_useTargetContext,
        card);
    m_targetContextControl->setObjectName(QStringLiteral("targetContextControl"));
    settings::addRow(cardLayout, m_targetContextControl, card);
    settings::addRow(cardLayout, settings::makeRow(QStringLiteral("Screenshots"), QStringLiteral("Allow screenshots as refinement context"), m_screenshotContext, card), card, false);

    auto *pageLayout = settings::makeSettingsPage(this);
    pageLayout->setSpacing(0);
    pageLayout->addWidget(title);
    pageLayout->addSpacing(settings::sectionGap());
    pageLayout->addWidget(card);
    pageLayout->addStretch();

    connect(m_provider, &QComboBox::currentIndexChanged, this, [this] {
        updateScreenshotControl();
        emit changed();
    });
    connect(m_writingProfile, &QComboBox::currentIndexChanged, this, &RefinementSettingsPage::changed);
    connect(m_useTargetContext, &QCheckBox::toggled, this, &RefinementSettingsPage::changed);
    connect(m_screenshotContext, &QCheckBox::toggled, this, &RefinementSettingsPage::changed);
}

void RefinementSettingsPage::setTargetAccessibilityAvailable(bool available)
{
    const QString explanation = available
        ? QString()
        : QStringLiteral("Enable desktop accessibility (AT-SPI) to use target-aware refinement.");
    m_targetContextControl->setEnabled(available);
    m_targetContextControl->setToolTip(explanation);
}

void RefinementSettingsPage::load(const AppSettings &settings)
{
    settings::selectData(m_provider, settings.refinement.providerId);
    settings::selectData(m_writingProfile, settings.refinement.defaultWritingProfile);
    setWritingProfileSettings(settings.refinement.writingProfiles);
    m_useTargetContext->setChecked(settings.refinement.useTargetContext);
    m_screenshotContext->setChecked(settings.refinement.includeScreenshotContext);
    updateScreenshotControl();
}

void RefinementSettingsPage::appendToDraft(AppSettings &draft) const
{
    draft.refinement.providerId = m_provider->currentData().toString();
    draft.refinement.defaultWritingProfile = m_writingProfile->currentData().toString();
    draft.refinement.writingProfiles = currentWritingProfileSettings();
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
