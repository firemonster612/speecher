#pragma once

#include <QString>

namespace speecher::SettingsKeys {

inline constexpr auto Organization = "io.github.firemonster612";
inline constexpr auto Application = "speecher";

inline const QString SetupCompleted = QStringLiteral("app/setupCompleted");
inline const QString LaunchAtLogin = QStringLiteral("app/launchAtLogin");
inline const QString UiPreviewWords = QStringLiteral("ui/previewWords");
inline const QString UiTheme = QStringLiteral("ui/theme");
inline const QString UiPauseMedia = QStringLiteral("ui/pauseMediaDuringTranscription");
inline const QString UiSoundsEnabled = QStringLiteral("ui/soundsEnabled");
inline const QString SpeechProvider = QStringLiteral("stt/provider");
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
inline const QString CorrectionLearningEnabled = QStringLiteral("vocabulary/correctionLearningEnabled");
inline const QString LearnedCorrections = QStringLiteral("vocabulary/learnedCorrections");
inline const QString CorrectionEvidence = QStringLiteral("vocabulary/correctionEvidence");
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
inline const QString CliproxyApiKey = QStringLiteral("cliproxy/apiKey");
inline const QString OutputMethod = QStringLiteral("output/method");
inline const QString OutputFormat = QStringLiteral("output/format");
inline const QString YdotoolEnabled = QStringLiteral("output/ydotoolEnabled");
inline const QString RestoreClipboardAfterTyping = QStringLiteral("output/restoreClipboardAfterTyping");
inline const QString CompletionStatusDurationMs = QStringLiteral("output/completionStatusDurationMs");
inline const QString PasteRules = QStringLiteral("output/pasteRules");
inline const QString UpdatesChannel = QStringLiteral("updates/channel");
inline const QString UpdatesAutoCheck = QStringLiteral("updates/autoCheck");
inline const QString UpdatesAutoInstall = QStringLiteral("updates/autoInstall");
inline const QString UpdatesLastCheckTime = QStringLiteral("updates/lastCheckTime");
inline const QString UpdatesDismissedVersion = QStringLiteral("updates/dismissedVersion");
inline const QString UpdatesLastRunVersion = QStringLiteral("updates/lastRunVersion");
inline const QString UpdatesPendingWhatsNewVersion = QStringLiteral("updates/pendingWhatsNewVersion");
inline const QString IdentityMigrationVersion = QStringLiteral("migration/identityVersion");
inline const QString ClaudeCredentialsPath = QStringLiteral("claude/credentialsPath");
inline const QString ClaudeEndpointBase = QStringLiteral("claude/endpointBase");
inline const QString ClaudeVoicePath = QStringLiteral("claude/voicePath");

} // namespace speecher::SettingsKeys
