#include "ui/settings/GeneralSettingsPage.h"

#include "ui/settings/SettingsPageSupport.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

namespace speecher {

GeneralSettingsPage::GeneralSettingsPage(const QString &primaryOutputStatus,
                                         QWidget *parent)
    : QScrollArea(parent)
    , m_theme(new QComboBox(this))
    , m_pauseMedia(new QCheckBox(this))
    , m_sounds(new QCheckBox(this))
    , m_previewWords(new QSpinBox(this))
{
    m_theme->addItem(QStringLiteral("System"), QStringLiteral("system"));
    m_theme->addItem(QStringLiteral("Light"), QStringLiteral("light"));
    m_theme->addItem(QStringLiteral("Dark"), QStringLiteral("dark"));
    m_theme->setObjectName(QStringLiteral("themeControl"));
    m_previewWords->setObjectName(QStringLiteral("previewWords"));
    m_previewWords->setRange(1, 40);

    auto *title = settings::makePageTitle(QStringLiteral("General"), this);
    auto *appearanceCard = settings::makeSettingsCard(this);
    auto *appearanceLayout = qobject_cast<QFormLayout *>(appearanceCard->layout());
    auto *systemCard = settings::makeSettingsCard(this);
    auto *systemLayout = qobject_cast<QFormLayout *>(systemCard->layout());
    auto *maintenanceCard = settings::makeSettingsCard(this);
    auto *maintenanceLayout = qobject_cast<QFormLayout *>(maintenanceCard->layout());
    auto *primaryOutput = new QLabel(primaryOutputStatus, this);
    primaryOutput->setObjectName(QStringLiteral("statusText"));
    primaryOutput->setForegroundRole(QPalette::WindowText);
    auto *openReleases = new QPushButton(QStringLiteral("Open releases"), this);
    auto *runSetup = new QPushButton(QStringLiteral("Run setup assistant…"), this);

    settings::addRow(appearanceLayout, settings::makeRow(QStringLiteral("Theme"), QString(), m_theme, appearanceCard), appearanceCard, true);
    settings::addRow(appearanceLayout, settings::makeRow(QStringLiteral("Media"), QStringLiteral("Pause playing media while dictating"), m_pauseMedia, appearanceCard), appearanceCard, true);
    settings::addRow(appearanceLayout, settings::makeRow(QStringLiteral("Sounds"), QStringLiteral("Play sounds when dictation starts and stops"), m_sounds, appearanceCard), appearanceCard, true);
    settings::addRow(appearanceLayout, settings::makeRow(QStringLiteral("Preview words"), QStringLiteral("Trailing words shown in the popup."), m_previewWords, appearanceCard), appearanceCard);
    settings::addRow(systemLayout, settings::makeRow(QStringLiteral("Clipboard output"), QStringLiteral("Current platform clipboard path."), primaryOutput, systemCard), systemCard);
    settings::addRow(maintenanceLayout, settings::makeRow(QStringLiteral("Setup assistant"), QStringLiteral("Check sign-in, microphone, accessibility, delivery, and refinement again."), runSetup, maintenanceCard), maintenanceCard, true);
    settings::addRow(maintenanceLayout, settings::makeRow(QStringLiteral("Updates"), QStringLiteral("Updates are manual; open the GitHub releases page when you want to check."), openReleases, maintenanceCard), maintenanceCard);

    auto *pageLayout = settings::makeSettingsPage(this);
    pageLayout->setSpacing(0);
    pageLayout->addWidget(title);
    pageLayout->addSpacing(settings::sectionGap());
    pageLayout->addWidget(settings::makeSectionLabel(QStringLiteral("Appearance & behavior"), this));
    pageLayout->addSpacing(settings::tightSpacing());
    pageLayout->addWidget(appearanceCard);
    pageLayout->addSpacing(settings::groupGap());
    pageLayout->addWidget(settings::makeSectionLabel(QStringLiteral("System"), this));
    pageLayout->addSpacing(settings::tightSpacing());
    pageLayout->addWidget(systemCard);
    pageLayout->addSpacing(settings::groupGap());
    pageLayout->addWidget(settings::makeSectionLabel(QStringLiteral("Maintenance"), this));
    pageLayout->addSpacing(settings::tightSpacing());
    pageLayout->addWidget(maintenanceCard);
    pageLayout->addStretch();

    connect(m_theme, &QComboBox::currentIndexChanged, this, &GeneralSettingsPage::changed);
    connect(m_pauseMedia, &QCheckBox::toggled, this, &GeneralSettingsPage::changed);
    connect(m_sounds, &QCheckBox::toggled, this, &GeneralSettingsPage::changed);
    connect(m_previewWords, &QSpinBox::valueChanged, this, &GeneralSettingsPage::changed);
    connect(openReleases, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/firemonster612/speecher/releases")));
    });
    connect(runSetup, &QPushButton::clicked, this, &GeneralSettingsPage::setupRequested);
}

void GeneralSettingsPage::load(const AppSettings &settings)
{
    settings::selectData(m_theme, settings.ui.theme);
    m_pauseMedia->setChecked(settings.ui.pauseMediaDuringTranscription);
    m_sounds->setChecked(settings.ui.soundsEnabled);
    m_previewWords->setValue(settings.ui.previewWords);
}

void GeneralSettingsPage::appendToDraft(AppSettings &draft) const
{
    draft.ui.theme = m_theme->currentData().toString();
    draft.ui.pauseMediaDuringTranscription = m_pauseMedia->isChecked();
    draft.ui.soundsEnabled = m_sounds->isChecked();
    draft.ui.previewWords = m_previewWords->value();
}

bool GeneralSettingsPage::hasChanges(const AppSettings &settings) const
{
    AppSettings draft = settings;
    appendToDraft(draft);
    return draft.ui.theme != settings.ui.theme
        || draft.ui.pauseMediaDuringTranscription != settings.ui.pauseMediaDuringTranscription
        || draft.ui.soundsEnabled != settings.ui.soundsEnabled
        || draft.ui.previewWords != settings.ui.previewWords;
}

} // namespace speecher
