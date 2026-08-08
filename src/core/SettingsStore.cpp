#include "core/SettingsStore.h"

namespace speecher {

SettingsStore::SettingsStore(QObject *parent)
    : QObject(parent)
    , SettingsCodecs()
{
}

void SettingsStore::applySnapshot(const AppSettings &draft)
{
    setTheme(draft.ui.theme);
    setPauseMediaDuringTranscription(draft.ui.pauseMediaDuringTranscription);
    setSoundsEnabled(draft.ui.soundsEnabled);
    setPreviewWords(draft.ui.previewWords);
    setAudioCaptureSettings(draft.audio);
    setRefinementProvider(draft.refinement.providerId);
    setDefaultWritingProfile(draft.refinement.defaultWritingProfile);
    setWritingProfileSettings(draft.refinement.writingProfiles);
    setWritingProfileOverrides(draft.refinement.writingProfileOverrides);
    setUseTargetContext(draft.refinement.useTargetContext);
    setIncludeScreenshotContext(draft.refinement.includeScreenshotContext);
    setOpenAiModel(draft.refinement.openAiModel);
    setOpenAiEffort(draft.refinement.openAiEffort);
    setAnthropicModel(draft.refinement.anthropicModel);
    setAnthropicEffort(draft.refinement.anthropicEffort);
    setOutputMethod(draft.output.method);
    setOutputFormat(draft.output.format);
    setPasteRules(draft.output.pasteRules);
    setRestoreClipboardAfterTyping(draft.output.restoreClipboardAfterTyping);
    setLearnedCorrections(draft.learnedCorrections);
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
