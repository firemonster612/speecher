#pragma once

#include "core/OutputFormat.h"
#include "core/PasteRules.h"
#include "core/LearnedCorrection.h"
#include "core/Target.h"

#include <QString>
#include <QStringList>
#include <QList>
#include <QMap>

#include <array>

namespace speecher {

enum class UpdateChannel {
    Stable,
    Nightly,
};

inline QString updateChannelName(UpdateChannel channel)
{
    return channel == UpdateChannel::Nightly ? QStringLiteral("nightly")
                                             : QStringLiteral("stable");
}

inline UpdateChannel updateChannelFromName(const QString &name)
{
    return name == QStringLiteral("nightly") ? UpdateChannel::Nightly
                                             : UpdateChannel::Stable;
}

// How the Global Shortcut drives a Dictation Session.
enum class ShortcutActivationMode {
    PushToTalk,
    Toggle,
    Hybrid,
};

inline QString shortcutActivationModeName(ShortcutActivationMode mode)
{
    switch (mode) {
    case ShortcutActivationMode::PushToTalk:
        return QStringLiteral("push_to_talk");
    case ShortcutActivationMode::Toggle:
        return QStringLiteral("toggle");
    case ShortcutActivationMode::Hybrid:
        break;
    }
    return QStringLiteral("hybrid");
}

// Anything unrecognised reads as hybrid, which is what the app did before the
// setting existed.
inline ShortcutActivationMode shortcutActivationModeFromName(const QString &name)
{
    if (name == QStringLiteral("push_to_talk")) {
        return ShortcutActivationMode::PushToTalk;
    }
    if (name == QStringLiteral("toggle")) {
        return ShortcutActivationMode::Toggle;
    }
    return ShortcutActivationMode::Hybrid;
}

struct BindingRule {
    QString phrase;
    QString replacement;

    bool operator==(const BindingRule &other) const = default;
};

struct VocabularyEntry {
    QString term;
    QString source = QStringLiteral("manual");
    // First in line for the speech service among key terms. No longer shown
    // or edited; a term starred before keeps its place.
    bool starred = false;
    int frequency = 0;
    qint64 lastUsedMs = 0;
    // What the term means and when it applies, for refinement. Empty for none.
    QString context;
    // The Writing Profiles the term is limited to, by id. Empty for every one.
    QStringList profiles;
    // Sent to the speech service as a hint. Refinement uses every term.
    bool keyTerm = true;

    bool operator==(const VocabularyEntry &other) const = default;
};

struct UiSettings {
    int previewWords = 7;
    bool transcriptionPreviewEnabled = true;
    bool refinementPreviewEnabled = true;
    QString theme = QStringLiteral("system");
    bool pauseMediaDuringTranscription = true;
    bool soundsEnabled = false;
};

// The backends Runs on can name: "auto", then transcribe.cpp's device kinds,
// each with what a person calls it. LocalSpeechEngine maps them to
// transcribe.cpp's requests in this order.
struct LocalBackend {
    const char *kind;
    const char *name;
};
inline constexpr std::array<LocalBackend, 6> localBackends{{
    {"auto", "Automatic"},
    {"cpu", "CPU"},
    {"metal", "Metal"},
    {"vulkan", "Vulkan"},
    {"cuda", "CUDA"},
    {"rocm", "ROCm"},
}};

inline QString localBackendName(const QString &kind)
{
    for (const LocalBackend &backend : localBackends) {
        if (kind == QLatin1String(backend.kind)) return QString::fromLatin1(backend.name);
    }
    return kind;
}

// Where Local Models run: the Local models page's Runs on choice.
struct LocalRunsOn {
    // A localBackends kind.
    QString backend = QStringLiteral("auto");
    // A transcribe.cpp device_id (PCI bus id) of that backend, or empty for
    // the backend's own choice. One card can appear under two backends. With
    // "auto", the card decides the backend: a card chosen before Runs on had
    // backends is kept that way.
    QString deviceId;

    bool operator==(const LocalRunsOn &other) const = default;
};

// "auto", "cpu", "cuda:0000:01:00.0": how Runs on stores a choice.
inline QString localRunsOnId(const LocalRunsOn &runsOn)
{
    return runsOn.deviceId.isEmpty() ? runsOn.backend
                                     : runsOn.backend + QLatin1Char(':') + runsOn.deviceId;
}

inline LocalRunsOn localRunsOnFromId(const QString &id)
{
    // PCI bus ids have colons of their own; the backend is what precedes the first.
    const qsizetype colon = id.indexOf(QLatin1Char(':'));
    if (colon < 0) {
        return {id, QString()};
    }
    return {id.left(colon), id.mid(colon + 1)};
}

// A Speed Test result's key: the model, and where it ran.
inline QString localSpeedTestKey(const QString &modelId, const LocalRunsOn &runsOn)
{
    return modelId + QLatin1Char('/') + localRunsOnId(runsOn);
}

struct LocalSpeechSettings {
    // A LocalModelCatalog id.
    QString modelId = QStringLiteral("parakeet");
    LocalRunsOn runsOn;
    // 0 keeps the model loaded until Speecher quits.
    int idleUnloadMinutes = 10;
    // localSpeedTestKey to the Speed Test's measured seconds for the bundled
    // clip, so a result from one Runs on choice never stands for another.
    QMap<QString, double> speedTestSeconds;
    // False only for an unchosen default; legacy saved model ids are choices.
    bool modelChosen = false;

    bool operator==(const LocalSpeechSettings &other) const = default;
};

// The speech Custom Endpoint: an OpenAI-style audio transcriptions server.
struct SpeechEndpointSettings {
    // The server's origin, without the path.
    QString baseUrl;
    // whisper.cpp's server answers on /inference.
    QString path = QStringLiteral("/v1/audio/transcriptions");
    QString model;
    QString apiKey;

    bool operator==(const SpeechEndpointSettings &other) const = default;
};

// The stored Spoken Language that leaves the language to the speech service.
inline constexpr auto kAutomaticSpokenLanguage = "auto";

struct SpeechSettings {
    QString providerId = QStringLiteral("claude");
    // Up to two providers tried in order when providerId is unavailable;
    // never providerId itself. See ProviderChain.h.
    QStringList fallbackProviderIds;
    QString claudeAuthMode = QStringLiteral("oauth");
    QString codexAuthMode = QStringLiteral("auto");
    bool codexFinalRetranscribe = true;
    // The Spoken Language: a language code, or kAutomaticSpokenLanguage.
    QString language = QStringLiteral("en");
    QStringList vocabulary;
    QString claudeCredentialsPath;
    QString claudeEndpointBase;
    QString claudeVoicePath;
    QString cliproxyOauthDir;
    QString claudeCliproxyAccount;
    QString codexCliproxyAccount;
    LocalSpeechSettings local;
    SpeechEndpointSettings endpoint;
    // Never stored. Transcribe sets it so a provider that can time its
    // transcript does; dictation leaves it off, because asking a model for
    // timings slows it and can change the text it returns.
    bool timedSegments = false;
};

struct AudioCaptureSettings {
    QString deviceId;
    QString mode = QStringLiteral("on_demand");
    bool vadEnabled = false;
    int preRollMs = 250;
    int postRollMs = 200;
    int readinessTimeoutMs = 900;
    int vadThresholdPercent = 2;

    bool operator==(const AudioCaptureSettings &other) const = default;
};

// The refinement Custom Endpoint.
struct RefinementEndpointSettings {
    // "cliproxy" takes the server and key from the CLI Proxy API settings;
    // empty uses the fields below.
    QString preset;
    // "openai" (Chat Completions) or "anthropic" (Messages).
    QString format = QStringLiteral("openai");
    // The API base the endpoint paths hang off, e.g. http://localhost:11434/v1.
    QString baseUrl;
    QString model;
    QString apiKey;

    // A detached URL can still use an unread proxy key until the user edits the key.
    bool useCliproxyKey = false;

    bool operator==(const RefinementEndpointSettings &other) const = default;
};

// Ultrafast serves only GPT-6 Astra so far. GPT-6.1 Sol is due to join it.
inline bool openAiModelSupportsUltrafast(const QString &model)
{
    return model.trimmed().toCaseFolded().startsWith(QStringLiteral("gpt-6-astra"));
}

// The OpenAI speed a refinement with this model runs at: Ultrafast asks for
// Fast on a model that has no Ultrafast tier.
inline QString effectiveOpenAiSpeed(const QString &speed, const QString &model)
{
    return speed == QStringLiteral("ultrafast") && !openAiModelSupportsUltrafast(model)
        ? QStringLiteral("fast")
        : speed;
}

// Refinement through a Local Runner on this computer.
struct LocalRunnerSettings {
    // "ollama", "lmstudio" or "llama-server"; empty until configured.
    QString runner;
    QString model;

    bool operator==(const LocalRunnerSettings &other) const = default;
};

// What a person calls a Local Runner stored by id.
inline QString localRunnerName(const QString &runnerId)
{
    if (runnerId == QStringLiteral("lmstudio")) return QStringLiteral("LM Studio");
    if (runnerId == QStringLiteral("llama-server")) return QStringLiteral("llama-server");
    return QStringLiteral("Ollama");
}

struct RefinementSettings {
    QString providerId = QStringLiteral("openai");
    // As SpeechSettings::fallbackProviderIds; empty while providerId is "none".
    QStringList fallbackProviderIds;
    QString style = QStringLiteral("balanced");
    QString openAiModel = QStringLiteral("gpt-6-luna");
    QString openAiAuthMode = QStringLiteral("auto");
    QString openAiEffort = QStringLiteral("none");
    // "standard", "fast" or "ultrafast"; see effectiveOpenAiSpeed.
    QString openAiSpeed = QStringLiteral("fast");
    QString openAiCliproxyAccount;
    QString anthropicModel = QStringLiteral("claude-opus-5-5");
    QString anthropicAuthMode = QStringLiteral("oauth");
    QString anthropicEffort = QStringLiteral("low");
    bool anthropicFastMode = true;
    QString anthropicCliproxyAccount;
    QString cliproxyOauthDir;
    // What the user typed, or empty for automatic detection. cliproxyOauthDir
    // above carries the resolved directory; saving that back would pin a
    // detected path forever, so the settings surface edits this one.
    QString cliproxyOauthDirConfigured;
    QString cliproxyBaseUrl;
    QString cliproxyApiKey;
    RefinementEndpointSettings endpoint;
    LocalRunnerSettings localRunner;
    QString anthropicEndpointBase = QStringLiteral("https://api.anthropic.com/v1");
    QString claudeCredentialsPath;
    QStringList bindingVocabulary;
    QString defaultWritingProfile = WritingProfile::Other;
    QList<WritingProfileSettings> writingProfiles = defaultWritingProfileSettings();
    QList<WritingProfileOverride> writingProfileOverrides;
    // The profile `toggle --profile` or `start --profile` forces for one
    // Dictation Session, ahead of every other rule; empty otherwise. Never saved.
    QString sessionWritingProfile;
    QString tone = QStringLiteral("none");
    bool useTargetContext = true;
    bool includeScreenshotContext = false;
    // Added to every refinement prompt.
    QString additionalInstructions;
    // Empty stands for the built-in prompt.
    bool customSystemPromptEnabled = false;
    QString customSystemPrompt;
    // Offered after the built-in tones and cleanup levels.
    QList<CustomTone> customTones;
    QList<CustomCleanupLevel> customCleanupLevels;
};

struct OutputSettings {
    QString method = QStringLiteral("automatic");
    OutputFormat format = OutputFormat::PlainText;
    bool ydotoolEnabled = false;
    bool restoreClipboardAfterTyping = false;
    int completionStatusDurationMs = 500;
    QList<PasteRule> pasteRules = defaultPasteRules();
};

struct UpdateSettings {
    UpdateChannel channel = UpdateChannel::Stable;
    bool autoCheck = true;
    bool autoInstall = false;
    int checkIntervalMinutes = 30;
};

struct AppSettings {
    // Empty secret fields in this draft were unread, not cleared by the user.
    QStringList unreadSecretKeys;
    bool setupCompleted = false;
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    bool launchAtLogin = true;
#else
    bool launchAtLogin = false;
#endif
    UiSettings ui;
    SpeechSettings speech;
    AudioCaptureSettings audio;
    QList<AppRecognitionRule> appRecognitionRules;
    RefinementSettings refinement;
    OutputSettings output;
    UpdateSettings updates;
    ShortcutActivationMode shortcutActivationMode = ShortcutActivationMode::Hybrid;
    QList<BindingRule> bindings;
    QList<VocabularyEntry> vocabulary;
    QList<LearnedCorrection> learnedCorrections;
    bool correctionLearningEnabled = true;
    bool insightsEnabled = true;
};

} // namespace speecher
