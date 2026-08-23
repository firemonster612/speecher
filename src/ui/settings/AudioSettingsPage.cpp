#include "ui/settings/AudioSettingsPage.h"

#include "app/LinuxComposition.h"
#include "providers/ProviderRegistry.h"
#include "ui/settings/SettingsPageSupport.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

namespace speecher {

AudioSettingsPage::AudioSettingsPage(const LinuxComposition &platform,
                                     const ProviderRegistry &providers,
                                     QWidget *parent)
    : QScrollArea(parent)
    , m_platform(platform)
    , m_speechProvider(new QComboBox(this))
    , m_speechAuthMode(new QComboBox(this))
    , m_audioDevice(new QComboBox(this))
    , m_captureMode(new QComboBox(this))
    , m_vadEnabled(new QCheckBox(this))
    , m_preRollMs(new QSpinBox(this))
    , m_postRollMs(new QSpinBox(this))
    , m_readinessTimeoutMs(new QSpinBox(this))
    , m_vadThreshold(new QSpinBox(this))
{
    m_speechProvider->setObjectName(QStringLiteral("speechProvider"));
    m_speechProvider->setMinimumContentsLength(24);
    for (const ProviderDescriptor &provider : providers.speechProviders()) {
        m_speechProvider->addItem(provider.label, provider.id);
    }
    m_speechAuthMode->setObjectName(QStringLiteral("speechAuthMode"));
    m_speechAuthMode->addItem(QStringLiteral("Provider default"), QStringLiteral("default"));
    m_speechAuthMode->addItem(QStringLiteral("CLI Proxy API"), QStringLiteral("cliproxy"));
    m_speechAuthMode->setToolTip(QStringLiteral(
        "Provider default uses the Claude Code or Codex CLI login. CLI Proxy API uses the OAuth "
        "account files CLI Proxy API keeps locally; the account is chosen on the Providers page."));
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
                     settings::makeRow(QStringLiteral("Transcription"),
                                       QStringLiteral("Service used to turn speech into a Raw Transcript."),
                                       m_speechProvider,
                                       card),
                     card);
    settings::addRow(cardLayout,
                     settings::makeRow(QStringLiteral("Speech credentials"),
                                       QStringLiteral("Credential source used by the transcription service."),
                                       m_speechAuthMode,
                                       card),
                     card);
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

    connect(m_speechProvider, &QComboBox::currentIndexChanged, this, &AudioSettingsPage::changed);
    connect(m_speechAuthMode, &QComboBox::currentIndexChanged, this, &AudioSettingsPage::changed);
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
    settings::selectData(m_speechProvider, settings.speech.providerId);
    settings::selectData(m_speechAuthMode, settings.speech.authMode);
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
    draft.speech.providerId = m_speechProvider->currentData().toString();
    draft.speech.authMode = m_speechAuthMode->currentData().toString();
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
    return draft.speech.providerId != settings.speech.providerId
        || draft.audio != settings.audio;
}

void AudioSettingsPage::refreshAudioDeviceList(const QString &selectedDeviceId)
{
    settings::populateAudioInputDevices(m_audioDevice,
                                        m_platform.availableAudioInputDevices(),
                                        selectedDeviceId);
}

void AudioSettingsPage::updateAudioControls()
{
    m_vadThreshold->setEnabled(m_vadEnabled->isChecked());
}

} // namespace speecher
