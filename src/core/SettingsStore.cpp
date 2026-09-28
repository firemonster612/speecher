#include "core/SettingsStore.h"
#include "core/SecretStore.h"
#include "core/settings/CorrectionSettingsCodec.h"
#include "core/settings/SettingsKeys.h"

#include <utility>

namespace speecher {

bool migrateSettingsIdentity(QSettings &newSettings, QSettings &oldSettings, QString *error)
{
    constexpr int currentMigrationVersion = 1;
    newSettings.setFallbacksEnabled(false);
    oldSettings.setFallbacksEnabled(false);
    if (newSettings.value(SettingsKeys::IdentityMigrationVersion).toInt()
        >= currentMigrationVersion) {
        return true;
    }

    const QStringList oldKeys = oldSettings.allKeys();
    const bool existingConfig = !newSettings.allKeys().isEmpty() || !oldKeys.isEmpty();
    for (const QString &key : oldKeys) {
        if (!newSettings.contains(key)) {
            newSettings.setValue(key, oldSettings.value(key));
        }
    }
    if (existingConfig && !newSettings.contains(SettingsKeys::UpdatesLastRunVersion)) {
        newSettings.setValue(SettingsKeys::UpdatesLastRunVersion, QStringLiteral("0.1.0"));
    }
    newSettings.sync();
    if (newSettings.status() != QSettings::NoError) {
        if (error) {
            *error = QStringLiteral("Could not persist migrated settings");
        }
        return false;
    }

    newSettings.setValue(SettingsKeys::IdentityMigrationVersion, currentMigrationVersion);
    newSettings.sync();
    if (newSettings.status() != QSettings::NoError) {
        if (error) {
            *error = QStringLiteral("Could not record settings migration");
        }
        return false;
    }
    return true;
}

void migrateRefinementModels(QSettings &settings)
{
    constexpr int currentMigrationVersion = 1;
    if (settings.value(SettingsKeys::RefinementModelMigrationVersion).toInt()
        >= currentMigrationVersion) {
        return;
    }
    const auto replace = [&settings](const QString &key, const QString &from, const QString &to) {
        if (settings.value(key).toString().trimmed() == from) {
            settings.setValue(key, to);
        }
    };
    replace(SettingsKeys::OpenAiModel, QStringLiteral("gpt-5.6-luna"), QStringLiteral("gpt-6-luna"));
    replace(SettingsKeys::AnthropicModel, QStringLiteral("claude-sonnet-5"),
            QStringLiteral("claude-opus-5-5"));
    settings.setValue(SettingsKeys::RefinementModelMigrationVersion, currentMigrationVersion);
}

SettingsStore::SettingsStore(QObject *parent)
    : QObject(parent)
    , SettingsCodecs()
{
    m_secrets = new SecretStore(this, this);
}

SecretStore *SettingsStore::secrets() const
{
    return m_secrets;
}

QString SettingsStore::cliproxyApiKey() const
{
    return m_secrets->secret(SecretStore::Secret::CliproxyApiKey);
}

void SettingsStore::setCliproxyApiKey(const QString &value)
{
    m_secrets->saveSecret(SecretStore::Secret::CliproxyApiKey, value);
}

AppSettings SettingsStore::snapshot() const
{
    return snapshotReading(true);
}

AppSettings SettingsStore::dictationSnapshot() const
{
    return snapshotReading(false);
}

QString SettingsStore::resolveDictationSecrets(AppSettings &settings)
{
    using Secret = SecretStore::Secret;
    QList<std::pair<Secret, QString *>> required;
    const auto require = [&](Secret secret, QString &value) {
        if (settings.unreadSecretKeys.contains(SecretStore::settingsKey(secret))) {
            required.append({secret, &value});
        }
    };
    auto &refinement = settings.refinement;
    if (settings.speech.providerId == QStringLiteral("endpoint")) {
        require(Secret::SpeechEndpointKey, settings.speech.endpoint.apiKey);
    }
    if (refinement.providerId == QStringLiteral("endpoint") && refinement.endpoint.preset.isEmpty()
        && !refinement.endpoint.useCliproxyKey) {
        require(Secret::RefinementEndpointKey, refinement.endpoint.apiKey);
    }
    // Speech uses local OAuth account files, never the remote proxy API key.
    const bool proxyPreset = refinement.providerId == QStringLiteral("endpoint")
        && (refinement.endpoint.preset == QStringLiteral("cliproxy") || refinement.endpoint.useCliproxyKey);
    const bool remoteProxy = !refinement.cliproxyBaseUrl.isEmpty()
        && ((refinement.providerId == QStringLiteral("openai") && refinement.openAiAuthMode == QStringLiteral("cliproxy"))
            || (refinement.providerId == QStringLiteral("anthropic") && refinement.anthropicAuthMode == QStringLiteral("cliproxy")));
    if (proxyPreset || remoteProxy) {
        require(Secret::CliproxyApiKey, refinement.cliproxyApiKey);
    }
    if (required.isEmpty()) return {};

    // Own QSettings and SecretStore in this worker; never access the GUI cache
    // or its in-flight prefetch jobs from another thread.
    SettingsStore source;
    for (const auto &[secret, value] : required) {
        *value = source.secrets()->secret(secret);
        if (value->isEmpty() && !source.secrets()->isSecretKnown(secret)) {
            return QStringLiteral("Desktop keyring unavailable for %1: %2")
                .arg(SecretStore::settingsKey(secret), source.secrets()->lastError());
        }
        settings.unreadSecretKeys.removeAll(SecretStore::settingsKey(secret));
    }
    return {};
}

// Preserve unread state in the draft even if prefetch completes before save.
AppSettings SettingsStore::snapshotReading(bool waitForKeyring) const
{
    AppSettings settings = SettingsCodecs::snapshot();
    const auto read = [this, &settings, waitForKeyring](SecretStore::Secret secret) {
        const QString value = waitForKeyring ? m_secrets->secret(secret) : m_secrets->cachedSecret(secret);
        if (!m_secrets->isSecretKnown(secret)) {
            settings.unreadSecretKeys.append(SecretStore::settingsKey(secret));
        }
        return value;
    };
    settings.refinement.cliproxyApiKey = read(SecretStore::Secret::CliproxyApiKey);
    settings.refinement.endpoint.apiKey =
        read(SecretStore::Secret::RefinementEndpointKey);
    settings.speech.endpoint.apiKey = read(SecretStore::Secret::SpeechEndpointKey);
    return settings;
}

void SettingsStore::applySnapshot(const AppSettings &draft)
{
    const AppSettings previous = dictationSnapshot();
    const auto save = [this, &draft](SecretStore::Secret secret, const QString &value) {
        if (value.trimmed().isEmpty() && draft.unreadSecretKeys.contains(SecretStore::settingsKey(secret))) return;
        m_secrets->saveSecret(secret, value);
    };
    setSetupCompleted(draft.setupCompleted);
    setLaunchAtLogin(draft.launchAtLogin);
    setTheme(draft.ui.theme);
    setPauseMediaDuringTranscription(draft.ui.pauseMediaDuringTranscription);
    setSoundsEnabled(draft.ui.soundsEnabled);
    setTranscriptionPreviewEnabled(draft.ui.transcriptionPreviewEnabled);
    setRefinementPreviewEnabled(draft.ui.refinementPreviewEnabled);
    setPreviewWords(draft.ui.previewWords);
    setSpeechProvider(draft.speech.providerId);
    setCodexFinalRetranscribe(draft.speech.codexFinalRetranscribe);
    setLocalSpeechSettings(draft.speech.local);
    setSpeechEndpointSettings(draft.speech.endpoint);
    save(SecretStore::Secret::SpeechEndpointKey, draft.speech.endpoint.apiKey);
    setAudioCaptureSettings(draft.audio);
    setAppRecognitionRules(draft.appRecognitionRules);
    setRefinementProvider(draft.refinement.providerId);
    setDefaultWritingProfile(draft.refinement.defaultWritingProfile);
    // Before the profiles, which fall back from a tone or level deleted here.
    setCustomTones(draft.refinement.customTones);
    setCustomCleanupLevels(draft.refinement.customCleanupLevels);
    setWritingProfileSettings(draft.refinement.writingProfiles);
    setWritingProfileOverrides(draft.refinement.writingProfileOverrides);
    setUseTargetContext(draft.refinement.useTargetContext);
    setIncludeScreenshotContext(draft.refinement.includeScreenshotContext);
    setAdditionalInstructions(draft.refinement.additionalInstructions);
    setCustomSystemPromptEnabled(draft.refinement.customSystemPromptEnabled);
    setCustomSystemPrompt(draft.refinement.customSystemPrompt);
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
    setCliproxyOauthDir(draft.refinement.cliproxyOauthDirConfigured);
    setCliproxyBaseUrl(draft.refinement.cliproxyBaseUrl);
    save(SecretStore::Secret::CliproxyApiKey, draft.refinement.cliproxyApiKey);
    setRefinementEndpointSettings(draft.refinement.endpoint);
    save(SecretStore::Secret::RefinementEndpointKey, draft.refinement.endpoint.apiKey);
    setLocalRunnerSettings(draft.refinement.localRunner);
    setOutputMethod(draft.output.method);
    setOutputFormat(draft.output.format);
    setPasteRules(draft.output.pasteRules);
    setRestoreClipboardAfterTyping(draft.output.restoreClipboardAfterTyping);
    setCompletionStatusDurationMs(draft.output.completionStatusDurationMs);
    setShortcutActivationMode(draft.shortcutActivationMode);
    setUpdateChannel(draft.updates.channel);
    setAutoCheckUpdates(draft.updates.autoCheck);
    setAutoInstallUpdates(draft.updates.autoInstall);
    setUpdateCheckIntervalMinutes(draft.updates.checkIntervalMinutes);
    setVocabularyEntries(draft.vocabulary);
    setLearnedCorrections(draft.learnedCorrections);
    setCorrectionLearningEnabled(draft.correctionLearningEnabled);
    setInsightsEnabled(draft.insightsEnabled);
    // The settings surface refuses invalid replacements before it saves, so a
    // rejection here is a bug rather than something a person typed.
    QString replacementError;
    if (!setBindingRules(draft.bindings, &replacementError)) {
        qWarning("dropped invalid replacement rules: %s", qPrintable(replacementError));
    }
    emit snapshotApplied(previous);
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
        emit launchAtLoginReconciliationFailed(
            error.isEmpty() ? QStringLiteral("This computer did not accept the change.") : error);
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

bool SettingsStore::audioVadEnabled() const
{
    return audioCaptureSettings().vadEnabled;
}

int SettingsStore::audioPreRollMs() const
{
    return audioCaptureSettings().preRollMs;
}

int SettingsStore::audioPostRollMs() const
{
    return audioCaptureSettings().postRollMs;
}

int SettingsStore::audioReadinessTimeoutMs() const
{
    return audioCaptureSettings().readinessTimeoutMs;
}

int SettingsStore::audioVadThresholdPercent() const
{
    return audioCaptureSettings().vadThresholdPercent;
}

void SettingsStore::setAudioCaptureSettings(const AudioCaptureSettings &value)
{
    const AudioCaptureSettings previous = audioCaptureSettings();
    SettingsCodecs::setAudioCaptureSettings(value);
    emitAudioCaptureSettingsChangedIfNeeded(previous);
}

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

void SettingsStore::setUpdateCheckIntervalMinutes(int value)
{
    if (updateCheckIntervalMinutes() == value) {
        return;
    }
    SettingsCodecs::setUpdateCheckIntervalMinutes(value);
    emit updateSettingsChanged();
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
