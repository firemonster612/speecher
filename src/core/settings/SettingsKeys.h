#pragma once

#include <QString>

namespace speecher::SettingsKeys {

inline constexpr auto Organization = "io.github.firemonster612";
inline constexpr auto Application = "speecher";

inline const QString SetupCompleted = QStringLiteral("app/setupCompleted");
inline const QString LaunchAtLogin = QStringLiteral("app/launchAtLogin");
inline const QString UiTranscriptionPreviewEnabled = QStringLiteral("ui/transcriptionPreviewEnabled");
inline const QString UiRefinementPreviewEnabled = QStringLiteral("ui/refinementPreviewEnabled");
inline const QString UiPreviewWords = QStringLiteral("ui/previewWords");
inline const QString UiTheme = QStringLiteral("ui/theme");
inline const QString UiPauseMedia = QStringLiteral("ui/pauseMediaDuringTranscription");
inline const QString UiSoundsEnabled = QStringLiteral("ui/soundsEnabled");
inline const QString SpeechProvider = QStringLiteral("stt/provider");
inline const QString CodexFinalRetranscribe = QStringLiteral("stt/codexFinalRetranscribe");
inline const QString LocalModelChosen = QStringLiteral("local/modelChosen");
inline const QString LocalPendingDownloads = QStringLiteral("local/pendingDownloads");
inline const QString LocalModel = QStringLiteral("local/model");
inline const QString LocalDevice = QStringLiteral("local/device");
inline const QString LocalIdleUnloadMinutes = QStringLiteral("local/idleUnloadMinutes");
// A group: one key per model id, holding the measured seconds for the clip.
inline const QString LocalSpeedTest = QStringLiteral("local/speedTest");
inline const QString VocabularyEntries = QStringLiteral("stt/vocabularyEntries");
inline const QString LegacyVocabulary = QStringLiteral("stt/customVocabulary");
inline const QString AudioDeviceId = QStringLiteral("audio/deviceId");
inline const QString AudioCaptureMode = QStringLiteral("audio/captureMode");
inline const QString AudioVadEnabled = QStringLiteral("audio/vadEnabled");
inline const QString AudioPreRollMs = QStringLiteral("audio/preRollMs");
inline const QString AudioPostRollMs = QStringLiteral("audio/postRollMs");
inline const QString AudioReadinessTimeoutMs = QStringLiteral("audio/readinessTimeoutMs");
inline const QString AudioVadThresholdPercent = QStringLiteral("audio/vadThresholdPercent");
inline const QString AppRecognitionRules = QStringLiteral("target/appRecognitionRules");
inline const QString BindingRules = QStringLiteral("bindings/rules");
// Only platforms without a desktop-wide shortcut registry of their own store the
// dictation binding here; KGlobalAccel owns it on KDE.
inline const QString GlobalShortcut = QStringLiteral("shortcuts/toggleDictation");
inline const QString ShortcutActivationMode = QStringLiteral("shortcuts/activationMode");
inline const QString CorrectionLearningEnabled = QStringLiteral("vocabulary/correctionLearningEnabled");
inline const QString LearnedCorrections = QStringLiteral("vocabulary/learnedCorrections");
inline const QString CorrectionEvidence = QStringLiteral("vocabulary/correctionEvidence");
inline const QString InsightsEnabled = QStringLiteral("insights/enabled");
inline const QString RefinementProvider = QStringLiteral("refinement/provider");
inline const QString RefinementStyle = QStringLiteral("refinement/style");
inline const QString DefaultWritingProfile = QStringLiteral("refinement/defaultWritingProfile");
inline const QString WritingProfiles = QStringLiteral("refinement/writingProfiles");
inline const QString WritingProfileOverrides = QStringLiteral("refinement/writingProfileOverrides");
inline const QString UseTargetContext = QStringLiteral("refinement/useTargetContext");
inline const QString IncludeScreenshotContext = QStringLiteral("refinement/includeScreenshotContext");
inline const QString OpenAiModel = QStringLiteral("openai/model");
inline const QString OpenAiAuthMode = QStringLiteral("openai/auth/mode");
inline const QString OpenAiEffort = QStringLiteral("openai/effort");
inline const QString OpenAiFastMode = QStringLiteral("openai/fastMode");
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
inline const QString RefinementEndpointUseCliproxyKey = QStringLiteral("endpoint/refinement/useCliproxyKey");
inline const QString RefinementEndpointPreset = QStringLiteral("endpoint/refinement/preset");
inline const QString RefinementEndpointFormat = QStringLiteral("endpoint/refinement/format");
inline const QString RefinementEndpointBaseUrl = QStringLiteral("endpoint/refinement/baseUrl");
inline const QString RefinementEndpointModel = QStringLiteral("endpoint/refinement/model");
inline const QString LocalRunner = QStringLiteral("localRunner/runner");
inline const QString LocalRunnerModel = QStringLiteral("localRunner/model");
inline const QString OutputMethod = QStringLiteral("output/method");
inline const QString OutputFormat = QStringLiteral("output/format");
inline const QString YdotoolEnabled = QStringLiteral("output/ydotoolEnabled");
inline const QString RestoreClipboardAfterTyping = QStringLiteral("output/restoreClipboardAfterTyping");
inline const QString CompletionStatusDurationMs = QStringLiteral("output/completionStatusDurationMs");
inline const QString PasteRules = QStringLiteral("output/pasteRules");
inline const QString UpdatesChannel = QStringLiteral("updates/channel");
inline const QString UpdatesAutoCheck = QStringLiteral("updates/autoCheck");
inline const QString UpdatesAutoInstall = QStringLiteral("updates/autoInstall");
inline const QString UpdatesCheckIntervalMinutes = QStringLiteral("updates/checkIntervalMinutes");
inline const QString UpdatesRestoreState = QStringLiteral("updates/restoreState");
inline const QString UpdatesRestoreStateTime = QStringLiteral("updates/restoreStateTime");
inline const QString UpdatesLastCheckTime = QStringLiteral("updates/lastCheckTime");
inline const QString UpdatesDismissedVersion = QStringLiteral("updates/dismissedVersion");
inline const QString UpdatesLastRunVersion = QStringLiteral("updates/lastRunVersion");
inline const QString UpdatesLastRunBuildNumber = QStringLiteral("updates/lastRunBuildNumber");
inline const QString UpdatesPendingWhatsNewVersion = QStringLiteral("updates/pendingWhatsNewVersion");
inline const QString IdentityMigrationVersion = QStringLiteral("migration/identityVersion");
inline const QString RefinementModelMigrationVersion = QStringLiteral("migration/refinementModelVersion");
inline const QString ClaudeCredentialsPath = QStringLiteral("claude/credentialsPath");
inline const QString ClaudeEndpointBase = QStringLiteral("claude/endpointBase");
inline const QString ClaudeVoicePath = QStringLiteral("claude/voicePath");

} // namespace speecher::SettingsKeys
