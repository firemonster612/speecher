#include "core/SettingsStore.h"
#include "core/settings/CorrectionSettingsCodec.h"

#include <utility>

namespace speecher {

SettingsStore::SettingsStore(QObject *parent)
    : QObject(parent)
    , SettingsCodecs()
{
}

void SettingsStore::applySnapshot(const AppSettings &draft)
{
    setSetupCompleted(draft.setupCompleted);
    setLaunchAtLogin(draft.launchAtLogin);
    setTheme(draft.ui.theme);
    setPauseMediaDuringTranscription(draft.ui.pauseMediaDuringTranscription);
    setSoundsEnabled(draft.ui.soundsEnabled);
    setPreviewWords(draft.ui.previewWords);
    setSpeechProvider(draft.speech.providerId);
    setAudioCaptureSettings(draft.audio);
    setAppRecognitionRules(draft.appRecognitionRules);
    setRefinementProvider(draft.refinement.providerId);
    setDefaultWritingProfile(draft.refinement.defaultWritingProfile);
    setWritingProfileSettings(draft.refinement.writingProfiles);
    setWritingProfileOverrides(draft.refinement.writingProfileOverrides);
    setUseTargetContext(draft.refinement.useTargetContext);
    setIncludeScreenshotContext(draft.refinement.includeScreenshotContext);
    setOpenAiModel(draft.refinement.openAiModel);
    setOpenAiEffort(draft.refinement.openAiEffort);
    setOpenAiFastMode(draft.refinement.openAiFastMode);
    setOpenAiAuthMode(draft.refinement.openAiAuthMode);
    setOpenAiCliproxyAccount(draft.refinement.openAiCliproxyAccount);
    setAnthropicModel(draft.refinement.anthropicModel);
    setAnthropicEffort(draft.refinement.anthropicEffort);
    setAnthropicFastMode(draft.refinement.anthropicFastMode);
    setAnthropicAuthMode(draft.refinement.anthropicAuthMode);
    setAnthropicCliproxyAccount(draft.refinement.anthropicCliproxyAccount);
    setCliproxyBaseUrl(draft.refinement.cliproxyBaseUrl);
    setCliproxyApiKey(draft.refinement.cliproxyApiKey);
    setOutputMethod(draft.output.method);
    setOutputFormat(draft.output.format);
    setPasteRules(draft.output.pasteRules);
    setRestoreClipboardAfterTyping(draft.output.restoreClipboardAfterTyping);
    setCompletionStatusDurationMs(draft.output.completionStatusDurationMs);
    setUpdateChannel(draft.updates.channel);
    setAutoCheckUpdates(draft.updates.autoCheck);
    setAutoInstallUpdates(draft.updates.autoInstall);
    setVocabularyEntries(draft.vocabulary);
    setLearnedCorrections(draft.learnedCorrections);
    setCorrectionLearningEnabled(draft.correctionLearningEnabled);
    // The settings surface refuses invalid replacements before it saves, so a
    // rejection here is a bug rather than something a person typed.
    QString replacementError;
    if (!setBindingRules(draft.bindings, &replacementError)) {
        qWarning("dropped invalid replacement rules: %s", qPrintable(replacementError));
    }
}

bool SettingsStore::launchAtLogin() const
{
    return SettingsCodecs::launchAtLogin();
}

void SettingsStore::setLaunchAtLogin(bool enabled)
{
    SettingsCodecs::setLaunchAtLogin(enabled);
    reconcileLaunchAtLogin();
}

void SettingsStore::setLaunchAtLoginReconciler(LaunchAtLoginReconciler reconcile)
{
    m_reconcileLaunchAtLogin = std::move(reconcile);
}

void SettingsStore::reconcileLaunchAtLogin()
{
    if (!m_reconcileLaunchAtLogin) {
        return;
    }
    QString error;
    if (!m_reconcileLaunchAtLogin(launchAtLogin(), &error)) {
        qWarning().noquote() << "launch at login reconciliation failed message=" + error;
    }
}

void SettingsStore::setCorrectionLearningEnabled(bool enabled)
{
    if (correctionLearningEnabled() == enabled) {
        return;
    }
    SettingsCodecs::setCorrectionLearningEnabled(enabled);
    emit correctionLearningEnabledChanged(enabled);
}

bool SettingsStore::recordCorrectionEvidence(const CorrectionEvidence &evidence,
                                             const QString &applicationId)
{
    if (!correctionLearningEnabled()) {
        return false;
    }
    return CorrectionSettingsCodec::recordEvidence(m_settings, evidence, applicationId);
}

QString SettingsStore::audioInputDeviceId() const
{
    return audioCaptureSettings().deviceId;
}

void SettingsStore::setAudioInputDeviceId(const QString &value)
{
    AudioCaptureSettings settings = audioCaptureSettings();
    settings.deviceId = value;
    setAudioCaptureSettings(settings);
}

QString SettingsStore::audioCaptureMode() const
{
    return audioCaptureSettings().mode;
}

void SettingsStore::setAudioCaptureMode(const QString &value)
{
    AudioCaptureSettings settings = audioCaptureSettings();
    settings.mode = value;
    setAudioCaptureSettings(settings);
}

bool SettingsStore::audioVadEnabled() const
{
    return audioCaptureSettings().vadEnabled;
}

void SettingsStore::setAudioVadEnabled(bool value)
{
    AudioCaptureSettings settings = audioCaptureSettings();
    settings.vadEnabled = value;
    setAudioCaptureSettings(settings);
}

int SettingsStore::audioPreRollMs() const
{
    return audioCaptureSettings().preRollMs;
}

void SettingsStore::setAudioPreRollMs(int value)
{
    AudioCaptureSettings settings = audioCaptureSettings();
    settings.preRollMs = value;
    setAudioCaptureSettings(settings);
}

int SettingsStore::audioPostRollMs() const
{
    return audioCaptureSettings().postRollMs;
}

void SettingsStore::setAudioPostRollMs(int value)
{
    AudioCaptureSettings settings = audioCaptureSettings();
    settings.postRollMs = value;
    setAudioCaptureSettings(settings);
}

int SettingsStore::audioReadinessTimeoutMs() const
{
    return audioCaptureSettings().readinessTimeoutMs;
}

void SettingsStore::setAudioReadinessTimeoutMs(int value)
{
    AudioCaptureSettings settings = audioCaptureSettings();
    settings.readinessTimeoutMs = value;
    setAudioCaptureSettings(settings);
}

int SettingsStore::audioVadThresholdPercent() const
{
    return audioCaptureSettings().vadThresholdPercent;
}

void SettingsStore::setAudioVadThresholdPercent(int value)
{
    AudioCaptureSettings settings = audioCaptureSettings();
    settings.vadThresholdPercent = value;
    setAudioCaptureSettings(settings);
}

void SettingsStore::setAudioCaptureSettings(const AudioCaptureSettings &value)
{
    const AudioCaptureSettings previous = audioCaptureSettings();
    SettingsCodecs::setAudioCaptureSettings(value);
    emitAudioCaptureSettingsChangedIfNeeded(previous);
}

#ifdef Q_OS_MACOS
void SettingsStore::setUpdateChannel(UpdateChannel value)
{
    if (updateChannel() == value) {
        return;
    }
    SettingsCodecs::setUpdateChannel(value);
    emit updateSettingsChanged();
}

void SettingsStore::setAutoCheckUpdates(bool value)
{
    if (autoCheckUpdates() == value) {
        return;
    }
    SettingsCodecs::setAutoCheckUpdates(value);
    emit updateSettingsChanged();
}

void SettingsStore::setAutoInstallUpdates(bool value)
{
    if (autoInstallUpdates() == value) {
        return;
    }
    SettingsCodecs::setAutoInstallUpdates(value);
    emit updateSettingsChanged();
}
#endif

QSettings &SettingsStore::raw()
{
    return m_settings;
}

void SettingsStore::emitAudioCaptureSettingsChangedIfNeeded(const AudioCaptureSettings &previous)
{
    const AudioCaptureSettings current = audioCaptureSettings();
    if (current != previous) {
        emit audioCaptureSettingsChanged(current);
    }
}

} // namespace speecher
