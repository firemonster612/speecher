#pragma once

#include <QString>

namespace speecher::SettingsKeys {

inline constexpr auto Organization = "io.github.firemonster612";

// SPEECHER_INSTANCE=<name> runs an isolated instance, as the packaging checks
// do: its settings, app data, keyring entries and single-instance socket are
// its own, apart from the person's, and it leaves launch at login alone.
inline constexpr auto InstanceVariable = "SPEECHER_INSTANCE";

inline bool isolatedInstance()
{
    return !qEnvironmentVariableIsEmpty(InstanceVariable);
}

// "speecher", or "speecher-<name>" in an isolated instance.
inline QString application()
{
    return isolatedInstance() ? QStringLiteral("speecher-") + qEnvironmentVariable(InstanceVariable)
                              : QStringLiteral("speecher");
}

inline const QString SetupCompleted = QStringLiteral("app/setupCompleted");
inline const QString LaunchAtLogin = QStringLiteral("app/launchAtLogin");
inline const QString UiTranscriptionPreviewEnabled = QStringLiteral("ui/transcriptionPreviewEnabled");
inline const QString UiRefinementPreviewEnabled = QStringLiteral("ui/refinementPreviewEnabled");
inline const QString UiSelectionEditReviewEnabled = QStringLiteral("ui/selectionEditReviewEnabled");
inline const QString UiPreviewWords = QStringLiteral("ui/previewWords");
inline const QString UiTheme = QStringLiteral("ui/theme");
inline const QString UiColorScheme = QStringLiteral("ui/colorScheme");
inline const QString UiTranslucency = QStringLiteral("ui/translucency");
inline const QString UiWindowLayout = QStringLiteral("ui/windowLayout");
inline const QString UiRoundedCorners = QStringLiteral("ui/roundedCorners");
inline const QString UiBoringMode = QStringLiteral("ui/boringMode");
inline const QString UiPauseMedia = QStringLiteral("ui/pauseMediaDuringTranscription");
inline const QString UiSoundsEnabled = QStringLiteral("ui/soundsEnabled");
// The speech Service Order, as JSON (SettingsCodecs::speechServices).
inline const QString SpeechServices = QStringLiteral("stt/services");
// Read only by migrateServiceOrder, and kept a release for a downgrade: the
// speech primary, its fallbacks and their languages, the Local Model and the
// speech server's model, replaced by SpeechServices; the refinement
// primary ("none" for none), its fallbacks, the per-provider models, efforts
// and speeds, the Local Runner's and the server's model, replaced by
// RefinementServices and RefinementEnabled; and the server's CLI Proxy
// preset, now its URL.
inline const QString SpeechProvider = QStringLiteral("stt/provider");
inline const QString SpeechFallbackProviders = QStringLiteral("stt/fallbackProviders");
// A group: one key per speech fallback's provider id, holding its Spoken Language.
inline const QString SpeechFallbackLanguages = QStringLiteral("stt/fallbackLanguages");
inline const QString CodexFinalRetranscribe = QStringLiteral("stt/codexFinalRetranscribe");
inline const QString SpokenLanguage = QStringLiteral("stt/language");
inline const QString LocalModelChosen = QStringLiteral("local/modelChosen");
inline const QString LocalPendingDownloads = QStringLiteral("local/pendingDownloads");
inline const QString LocalModel = QStringLiteral("local/model");
inline const QString LocalBackend = QStringLiteral("local/backend");
inline const QString LocalDevice = QStringLiteral("local/device");
inline const QString LocalIdleUnloadMinutes = QStringLiteral("local/idleUnloadMinutes");
// A group: one key per localSpeedTestKey, holding the measured seconds for the clip.
inline const QString LocalSpeedTest = QStringLiteral("local/speedTest");
// Retired: migrateVocabulary moved them, BindingRules and LearnedCorrections
// into Vocabulary, and leaves them for a downgrade to read.
inline const QString VocabularyEntries = QStringLiteral("stt/vocabularyEntries");
inline const QString LegacyVocabulary = QStringLiteral("stt/customVocabulary");
inline const QString AudioDeviceId = QStringLiteral("audio/deviceId");
inline const QString AudioCaptureMode = QStringLiteral("audio/captureMode");
inline const QString AudioVadEnabled = QStringLiteral("audio/vadEnabled");
inline const QString AudioPreRollMs = QStringLiteral("audio/preRollMs");
inline const QString AudioPostRollMs = QStringLiteral("audio/postRollMs");
inline const QString AudioReadinessTimeoutMs = QStringLiteral("audio/readinessTimeoutMs");
inline const QString AudioVadThresholdPercent = QStringLiteral("audio/vadThresholdPercent");
// Retired: migrateApps moved them into AppsEntries, and leaves them for a
// downgrade to read.
inline const QString AppRecognitionRules = QStringLiteral("target/appRecognitionRules");
// Retired, as VocabularyEntries.
inline const QString BindingRules = QStringLiteral("bindings/rules");
// Only platforms without a desktop-wide shortcut registry of their own store the
// dictation binding here; KGlobalAccel owns it on KDE.
inline const QString GlobalShortcut = QStringLiteral("shortcuts/toggleDictation");
// The optional Global Shortcuts that cancel and pause dictation, kept the same way.
inline const QString CancelShortcut = QStringLiteral("shortcuts/cancelDictation");
inline const QString PauseShortcut = QStringLiteral("shortcuts/pauseDictation");
inline const QString PasteAgainShortcut = QStringLiteral("shortcuts/pasteAgain");
inline const QString ShortcutActivationMode = QStringLiteral("shortcuts/activationMode");
inline const QString CorrectionLearningEnabled = QStringLiteral("vocabulary/correctionLearningEnabled");
// Retired, as VocabularyEntries.
inline const QString LearnedCorrections = QStringLiteral("vocabulary/learnedCorrections");
// Every Vocabulary Entry, as JSON.
inline const QString Vocabulary = QStringLiteral("vocabulary/entries");
inline const QString CorrectionEvidence = QStringLiteral("vocabulary/correctionEvidence");
// Retired: migrateHistory turned it into HistoryKeeps, and leaves it for a
// release so a downgrade still reads it.
inline const QString InsightsEnabled = QStringLiteral("insights/enabled");
// What Speecher Keeps (historyKeepName), and how many days text and audio stay.
inline const QString HistoryKeeps = QStringLiteral("history/keeps");
inline const QString HistoryTextDays = QStringLiteral("history/textDays");
inline const QString HistoryAudioDays = QStringLiteral("history/audioDays");
// The refinement Service Order, as JSON, and Clean up my words.
inline const QString RefinementServices = QStringLiteral("refinement/services");
inline const QString RefinementEnabled = QStringLiteral("refinement/enabled");
inline const QString RefinementProvider = QStringLiteral("refinement/provider");
inline const QString RefinementFallbackProviders = QStringLiteral("refinement/fallbackProviders");
inline const QString RefinementStyle = QStringLiteral("refinement/style");
// Retired, as AppRecognitionRules: Other apps' profile in AppsTypes.
inline const QString DefaultWritingProfile = QStringLiteral("refinement/defaultWritingProfile");
inline const QString WritingProfiles = QStringLiteral("refinement/writingProfiles");
// Retired, as AppRecognitionRules.
inline const QString WritingProfileOverrides = QStringLiteral("refinement/writingProfileOverrides");
inline const QString UseTargetContext = QStringLiteral("refinement/useTargetContext");
inline const QString IncludeScreenshotContext = QStringLiteral("refinement/includeScreenshotContext");
inline const QString AdditionalInstructions = QStringLiteral("refinement/additionalInstructions");
inline const QString CustomSystemPromptEnabled = QStringLiteral("refinement/customSystemPromptEnabled");
inline const QString CustomSystemPrompt = QStringLiteral("refinement/customSystemPrompt");
inline const QString CustomTones = QStringLiteral("refinement/customTones");
inline const QString CustomCleanupLevels = QStringLiteral("refinement/customCleanupLevels");
inline const QString OpenAiModel = QStringLiteral("openai/model");
inline const QString OpenAiAuthMode = QStringLiteral("openai/auth/mode");
inline const QString OpenAiEffort = QStringLiteral("openai/effort");
// Replaced by OpenAiSpeed; read only when that is unset.
inline const QString OpenAiFastMode = QStringLiteral("openai/fastMode");
inline const QString OpenAiSpeed = QStringLiteral("openai/speed");
inline const QString OpenAiApiKey = QStringLiteral("openai/apiKey");
inline const QString OpenAiCliproxyAccount = QStringLiteral("openai/cliproxyAccount");
inline const QString AnthropicModel = QStringLiteral("anthropic/model");
inline const QString AnthropicAuthMode = QStringLiteral("anthropic/auth/mode");
inline const QString AnthropicEffort = QStringLiteral("anthropic/effort");
inline const QString AnthropicFastMode = QStringLiteral("anthropic/fastMode");
inline const QString AnthropicCliproxyAccount = QStringLiteral("anthropic/cliproxyAccount");
inline const QString CliproxyOauthDir = QStringLiteral("cliproxy/oauthDir");
inline const QString CliproxyBaseUrl = QStringLiteral("cliproxy/baseUrl");
// The settings-file copy of a secret is only a fallback for machines without
// a keyring; SecretStore owns these four keys.
inline const QString CliproxyApiKey = QStringLiteral("cliproxy/apiKey");
inline const QString SpeechEndpointApiKey = QStringLiteral("endpoint/speech/apiKey");
inline const QString RefinementEndpointApiKey = QStringLiteral("endpoint/refinement/apiKey");
// The keyring entries SecretStore has written besides the OpenAI key.
inline const QString SecretsInKeyring = QStringLiteral("secrets/inKeyring");
inline const QString SpeechEndpointBaseUrl = QStringLiteral("endpoint/speech/baseUrl");
inline const QString SpeechEndpointPath = QStringLiteral("endpoint/speech/path");
inline const QString SpeechEndpointModel = QStringLiteral("endpoint/speech/model");
// Retired; removed on save.
inline const QString RefinementEndpointUseCliproxyKey = QStringLiteral("endpoint/refinement/useCliproxyKey");
inline const QString RefinementEndpointPreset = QStringLiteral("endpoint/refinement/preset");
inline const QString RefinementEndpointFormat = QStringLiteral("endpoint/refinement/format");
inline const QString RefinementEndpointBaseUrl = QStringLiteral("endpoint/refinement/baseUrl");
inline const QString RefinementEndpointModel = QStringLiteral("endpoint/refinement/model");
inline const QString LocalRunner = QStringLiteral("localRunner/runner");
inline const QString LocalRunnerModel = QStringLiteral("localRunner/model");
// Retired: the delivery steps replaced them, and migrateDeliverySteps
// removes them.
inline const QString OutputMethod = QStringLiteral("output/method");
inline const QString RestoreClipboardAfterTyping = QStringLiteral("output/restoreClipboardAfterTyping");
inline const QString InsertThroughAccessibility = QStringLiteral("output/insertThroughAccessibility");
inline const QString PressPasteKeys = QStringLiteral("output/pressPasteKeys");
inline const QString KeepOnClipboard = QStringLiteral("output/keepOnClipboard");
inline const QString OutputFormat = QStringLiteral("output/format");
inline const QString YdotoolEnabled = QStringLiteral("output/ydotoolEnabled");
inline const QString CompletionStatusDurationMs = QStringLiteral("output/completionStatusDurationMs");
// Retired, as AppRecognitionRules: single apps' rules went to AppsEntries,
// app types' to AppsTypes.
inline const QString PasteRules = QStringLiteral("output/pasteRules");
// The person's Apps and what each App Type sets (AppsSettingsCodec).
inline const QString AppsEntries = QStringLiteral("apps/entries");
inline const QString AppsTypes = QStringLiteral("apps/types");
inline const QString UpdatesChannel = QStringLiteral("updates/channel");
inline const QString UpdatesAutoCheck = QStringLiteral("updates/autoCheck");
inline const QString UpdatesAutoInstall = QStringLiteral("updates/autoInstall");
inline const QString UpdatesCheckIntervalMinutes = QStringLiteral("updates/checkIntervalMinutes");
inline const QString UpdatesCheckIntervalUnit = QStringLiteral("updates/checkIntervalUnit");
inline const QString UpdatesRestoreState = QStringLiteral("updates/restoreState");
inline const QString UpdatesRestoreStateTime = QStringLiteral("updates/restoreStateTime");
inline const QString UpdatesLastCheckTime = QStringLiteral("updates/lastCheckTime");
inline const QString UpdatesDismissedVersion = QStringLiteral("updates/dismissedVersion");
inline const QString UpdatesLastRunVersion = QStringLiteral("updates/lastRunVersion");
inline const QString UpdatesLastRunBuildNumber = QStringLiteral("updates/lastRunBuildNumber");
inline const QString UpdatesPendingWhatsNewVersion = QStringLiteral("updates/pendingWhatsNewVersion");
inline const QString RecordingConsentNoticeShown = QStringLiteral("recording/consentNoticeShown");
inline const QString IdentityMigrationVersion = QStringLiteral("migration/identityVersion");
inline const QString RefinementModelMigrationVersion = QStringLiteral("migration/refinementModelVersion");
inline const QString DeliveryStepsMigrationVersion = QStringLiteral("migration/deliveryStepsVersion");
inline const QString AppsMigrationVersion = QStringLiteral("migration/appsVersion");
inline const QString ServiceOrderMigrationVersion = QStringLiteral("migration/serviceOrderVersion");
inline const QString VocabularyMigrationVersion = QStringLiteral("migration/vocabularyVersion");
inline const QString HistoryMigrationVersion = QStringLiteral("migration/historyVersion");
inline const QString ClaudeCredentialsPath = QStringLiteral("claude/credentialsPath");
inline const QString ClaudeEndpointBase = QStringLiteral("claude/endpointBase");
inline const QString ClaudeVoicePath = QStringLiteral("claude/voicePath");

} // namespace speecher::SettingsKeys
