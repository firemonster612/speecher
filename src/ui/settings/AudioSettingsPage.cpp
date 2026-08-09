#include "ui/settings/AudioSettingsPage.h"

#include "app/LinuxComposition.h"
#include "ui/settings/SettingsPageSupport.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

namespace speecher {

AudioSettingsPage::AudioSettingsPage(const LinuxComposition &platform,
                                     QWidget *parent)
    : QScrollArea(parent)
    , m_platform(platform)
    , m_audioDevice(new QComboBox(this))
    , m_captureMode(new QComboBox(this))
    , m_vadEnabled(new QCheckBox(this))
    , m_preRollMs(new QSpinBox(this))
    , m_postRollMs(new QSpinBox(this))
    , m_readinessTimeoutMs(new QSpinBox(this))
    , m_vadThreshold(new QSpinBox(this))
{
    m_audioDevice->setMinimumContentsLength(28);
    m_audioDevice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_audioDevice->setToolTip(QStringLiteral("Microphone Speecher records from."));
    m_captureMode->addItem(QStringLiteral("On demand"), QStringLiteral("on_demand"));
    m_captureMode->addItem(QStringLiteral("Warm"), QStringLiteral("warm"));
    m_captureMode->setToolTip(QStringLiteral("Warm keeps the microphone stream open between captures for lower startup latency."));
    m_vadEnabled->setToolTip(QStringLiteral("Suppress leading, trailing, and long in-between silence before audio is sent."));
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

    auto *title = settings::makePageTitle(QStringLiteral("Audio"), this);
    auto *card = settings::makeSettingsCard(this);
    auto *cardLayout = qobject_cast<QFormLayout *>(card->layout());

    settings::addRow(cardLayout,
                     settings::makeRow(QStringLiteral("Microphone"),
                                       QStringLiteral("Input device used for dictation."),
                                       m_audioDevice,
                                       card),
                     card);
    settings::addRow(cardLayout,
                     settings::makeRow(QStringLiteral("Capture mode"),
                                       QStringLiteral("Open the microphone only while listening, or keep it warm between captures."),
                                       m_captureMode,
                                       card),
                     card);
    settings::addRow(cardLayout,
                     settings::makeRow(QStringLiteral("Pre-roll"),
                                       QStringLiteral("Audio kept before speech or before a warm capture starts."),
                                       m_preRollMs,
                                       card),
                     card);
    settings::addRow(cardLayout,
                     settings::makeRow(QStringLiteral("Post-roll"),
                                       QStringLiteral("Audio kept after stop or after speech falls quiet."),
                                       m_postRollMs,
                                       card),
                     card);
    settings::addRow(cardLayout,
                     settings::makeRow(QStringLiteral("Readiness timeout"),
                                       QStringLiteral("How long Speecher waits for the first microphone sample."),
                                       m_readinessTimeoutMs,
                                       card),
                     card);
    settings::addRow(cardLayout,
                     settings::makeRow(QStringLiteral("Silence"),
                                       QStringLiteral("Trim leading and trailing silence"),
                                       m_vadEnabled,
                                       card),
                     card);
    settings::addRow(cardLayout,
                     settings::makeRow(QStringLiteral("Voice threshold"),
                                       QStringLiteral("RMS level required before VAD treats audio as speech."),
                                       m_vadThreshold,
                                       card),
                     card,
                     false);

    auto *pageLayout = settings::makeSettingsPage(this);
    pageLayout->setSpacing(0);
    pageLayout->addWidget(title);
    pageLayout->addSpacing(settings::sectionGap());
    pageLayout->addWidget(card);
    pageLayout->addStretch();

    connect(m_audioDevice, &QComboBox::currentIndexChanged, this, &AudioSettingsPage::changed);
    connect(m_captureMode, &QComboBox::currentIndexChanged, this, &AudioSettingsPage::changed);
    connect(m_vadEnabled, &QCheckBox::toggled, this, [this] {
        updateAudioControls();
        emit changed();
    });
    connect(m_preRollMs, &QSpinBox::valueChanged, this, &AudioSettingsPage::changed);
    connect(m_postRollMs, &QSpinBox::valueChanged, this, &AudioSettingsPage::changed);
    connect(m_readinessTimeoutMs, &QSpinBox::valueChanged, this, &AudioSettingsPage::changed);
    connect(m_vadThreshold, &QSpinBox::valueChanged, this, &AudioSettingsPage::changed);
}

void AudioSettingsPage::load(const AppSettings &settings)
{
    const AudioCaptureSettings &audio = settings.audio;
    refreshAudioDeviceList(audio.deviceId);
    settings::selectData(m_captureMode, audio.mode);
    m_vadEnabled->setChecked(audio.vadEnabled);
    m_preRollMs->setValue(audio.preRollMs);
    m_postRollMs->setValue(audio.postRollMs);
    m_readinessTimeoutMs->setValue(audio.readinessTimeoutMs);
    m_vadThreshold->setValue(audio.vadThresholdPercent);
    updateAudioControls();
}

void AudioSettingsPage::appendToDraft(AppSettings &draft) const
{
    draft.audio = {
        m_audioDevice->currentData().toString(),
        m_captureMode->currentData().toString(),
        m_vadEnabled->isChecked(),
        m_preRollMs->value(),
        m_postRollMs->value(),
        m_readinessTimeoutMs->value(),
        m_vadThreshold->value(),
    };
}

bool AudioSettingsPage::hasChanges(const AppSettings &settings) const
{
    AppSettings draft = settings;
    appendToDraft(draft);
    return draft.audio != settings.audio;
}

void AudioSettingsPage::refreshAudioDeviceList(const QString &selectedDeviceId)
{
    const QSignalBlocker blocker(m_audioDevice);
    m_audioDevice->clear();

    const QList<AudioInputDeviceInfo> devices = m_platform.availableAudioInputDevices();
    if (devices.isEmpty()) {
        m_audioDevice->addItem(QStringLiteral("No microphones found"), QString());
        settings::setComboItemEnabled(m_audioDevice,
                                      0,
                                      false,
                                      QStringLiteral("Connect or enable an input device, then reopen Settings."));
        if (!selectedDeviceId.isEmpty()) {
            m_audioDevice->addItem(QStringLiteral("Missing microphone"), selectedDeviceId);
            settings::setComboItemEnabled(m_audioDevice,
                                          1,
                                          false,
                                          QStringLiteral("This saved microphone is not currently available."));
            settings::selectData(m_audioDevice, selectedDeviceId);
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
        settings::setComboItemEnabled(m_audioDevice,
                                      missingIndex,
                                      false,
                                      QStringLiteral("This saved microphone is not currently available."));
    }

    settings::selectData(m_audioDevice, selectedDeviceId);
}

void AudioSettingsPage::updateAudioControls()
{
    m_vadThreshold->setEnabled(m_vadEnabled->isChecked());
}

} // namespace speecher
