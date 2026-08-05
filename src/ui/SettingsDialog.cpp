#include "ui/SettingsDialog.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "platform/PlatformIntegration.h"
#include "ui/Theme.h"
#include "ui/settings/BindingsSettingsPage.h"
#include "ui/settings/CorrectionsSettingsPage.h"
#include "ui/settings/GeneralSettingsPage.h"
#include "ui/settings/OutputSettingsPage.h"
#include "ui/settings/ProviderSettingsPage.h"
#include "ui/settings/RefinementSettingsPage.h"
#include "ui/settings/SettingsPageSupport.h"
#include "ui/settings/VocabularySettingsPage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace speecher {

using namespace settings;

SettingsDialog::SettingsDialog(ApplicationController *controller, QWidget *parent)
    : QDialog(parent)
    , m_controller(controller)
    , m_audioDevice(new QComboBox(this))
    , m_captureMode(new QComboBox(this))
    , m_vadEnabled(new QCheckBox(this))
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
    auto *vocabularySection = makeSectionLabel(QStringLiteral("Vocabulary"), this);
    auto *correctionsSection = makeSectionLabel(QStringLiteral("Learned Corrections"), this);
    auto *bindingsSection = makeSectionLabel(QStringLiteral("Replacements & snippets"), this);

    auto *audioCard = makeSettingsCard(this);
    auto *audioLayout = qobject_cast<QVBoxLayout *>(audioCard->layout());

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

    m_vocabularyPage = new VocabularySettingsPage(this);
    m_correctionsPage = new CorrectionsSettingsPage(this);
    m_bindingsPage = new BindingsSettingsPage(m_scroll, this);

    m_generalPage = new GeneralSettingsPage(m_controller->primaryOutputStatus(), this);
    m_outputPage = new OutputSettingsPage(*m_controller->settings(), this);
    m_providerPage = new ProviderSettingsPage(*m_controller->settings(), *m_controller->secretStore(), this);
    m_refinementPage = new RefinementSettingsPage(*m_controller->providerRegistry(), this);

    auto *audioPage = new QScrollArea(this);
    auto *audioPageLayout = makeSettingsPage(audioPage);
    audioPageLayout->addWidget(audioSection);
    audioPageLayout->addWidget(audioCard);
    audioPageLayout->addStretch();

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
        m_providerPage,
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
    connect(m_correctionsPage, &CorrectionsSettingsPage::changed, this, &SettingsDialog::updateButtonState);
    connect(m_outputPage, &OutputSettingsPage::changed, this, &SettingsDialog::updateButtonState);
    connect(m_providerPage, &ProviderSettingsPage::changed, this, &SettingsDialog::updateButtonState);
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
    m_providerPage->loadModels();
    m_outputPage->load(settings->snapshot());
    m_providerPage->loadAuth();
    m_vocabularyPage->load(settings->vocabularyEntries());
    m_bindingsPage->load(settings->bindingRules());
    m_correctionsPage->load(settings->correctionLearningEnabled(), settings->learnedCorrections());
    updateAudioControls();
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
    m_providerPage->saveModels();
    settings->setOutputMethod(draft.output.method);
    settings->setOutputFormat(draft.output.format);
    settings->setPasteRules(draft.output.pasteRules);
    settings->setRestoreClipboardAfterTyping(draft.output.restoreClipboardAfterTyping);
    m_providerPage->saveAuthModes();
    settings->setVocabularyEntries(m_vocabularyPage->entries());
    settings->setCorrectionLearningEnabled(m_correctionsPage->learningEnabled());
    settings->setLearnedCorrections(m_correctionsPage->corrections());
    settings->setBindingRules(bindingRules);
    m_vocabularyPage->load(settings->vocabularyEntries());
    m_bindingsPage->load(settings->bindingRules());
    m_correctionsPage->load(settings->correctionLearningEnabled(), settings->learnedCorrections());
    if (!m_providerPage->saveSecret()) {
        return false;
    }
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
        || m_providerPage->hasModelChanges()
        || m_outputPage->hasChanges(settings->snapshot())
        || m_providerPage->hasAuthChanges()
        || m_vocabularyPage->hasChanges(settings->vocabularyEntries())
        || m_correctionsPage->hasChanges(settings->correctionLearningEnabled(), settings->learnedCorrections())
        || m_bindingsPage->hasChanges(settings->bindingRules())) {
        return true;
    }

    return false;
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
