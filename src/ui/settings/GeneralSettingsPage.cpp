#include "ui/settings/GeneralSettingsPage.h"

#include "ui/settings/SettingsPageSupport.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
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
    m_pauseMedia->setText(QStringLiteral("Pause"));
    m_sounds->setText(QStringLiteral("Play sounds"));
    m_previewWords->setObjectName(QStringLiteral("previewWords"));
    m_previewWords->setRange(1, 40);

    auto *section = settings::makeSectionLabel(QStringLiteral("General"), this);
    auto *card = settings::makeSettingsCard(this);
    auto *cardLayout = qobject_cast<QVBoxLayout *>(card->layout());
    auto *primaryOutput = new QLabel(primaryOutputStatus, this);
    primaryOutput->setObjectName(QStringLiteral("statusText"));
    primaryOutput->setForegroundRole(QPalette::WindowText);
    auto *openReleases = new QPushButton(QStringLiteral("Open releases"), this);
    auto *runSetup = new QPushButton(QStringLiteral("Run setup assistant…"), this);

    settings::addRow(cardLayout, settings::makeRow(QStringLiteral("Theme"), QStringLiteral("App colors."), m_theme, card), card);
    settings::addRow(cardLayout, settings::makeRow(QStringLiteral("Pause media"), QStringLiteral("Pause currently playing media while transcribing."), m_pauseMedia, card), card);
    settings::addRow(cardLayout, settings::makeRow(QStringLiteral("Sound cues"), QStringLiteral("Use the desktop sound for recording start and stop."), m_sounds, card), card);
    settings::addRow(cardLayout, settings::makeRow(QStringLiteral("Preview words"), QStringLiteral("Trailing words shown in the popup."), m_previewWords, card), card);
    settings::addRow(cardLayout, settings::makeRow(QStringLiteral("Clipboard output"), QStringLiteral("Current platform clipboard path."), primaryOutput, card), card);
    settings::addRow(cardLayout, settings::makeRow(QStringLiteral("Setup assistant"), QStringLiteral("Check sign-in, microphone, accessibility, delivery, and refinement again."), runSetup, card), card);
    settings::addRow(cardLayout, settings::makeRow(QStringLiteral("Updates"), QStringLiteral("Updates are manual; open the GitHub releases page when you want to check."), openReleases, card), card, false);

    auto *pageLayout = settings::makeSettingsPage(this);
    pageLayout->addWidget(section);
    pageLayout->addWidget(card);
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
