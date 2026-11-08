#pragma once

#include "core/DeliverySteps.h"
#include "core/OutputFormat.h"
#include "core/Apps.h"
#include "core/history/HistoryPolicy.h"
#include "core/ProviderTable.h"
#include "core/ServiceOrder.h"
#include "core/Target.h"

#include <QString>
#include <QStringList>
#include <QList>
#include <QMap>

#include <array>
#include <optional>

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

// Where OpenAI sign-ins come from. Auto takes the first one found: the Codex
// app's ChatGPT sign-in or API key, then OPENAI_API_KEY, then the key saved
// in Speecher.
enum class OpenAiAuthMode {
    Auto,
    CodexOauth,
    CodexApiKey,
    Env,
    Settings,
    Cliproxy,
};

inline QString openAiAuthModeName(OpenAiAuthMode mode)
{
    switch (mode) {
    case OpenAiAuthMode::CodexOauth:
        return QStringLiteral("codex_oauth");
    case OpenAiAuthMode::CodexApiKey:
        return QStringLiteral("codex_api_key");
    case OpenAiAuthMode::Env:
        return QStringLiteral("env");
    case OpenAiAuthMode::Settings:
        return QStringLiteral("settings");
    case OpenAiAuthMode::Cliproxy:
        return QStringLiteral("cliproxy");
    case OpenAiAuthMode::Auto:
        break;
    }
    return QStringLiteral("auto");
}

// Anything unrecognised reads as Auto.
inline OpenAiAuthMode openAiAuthModeFromName(const QString &name)
{
    for (const OpenAiAuthMode mode : {OpenAiAuthMode::CodexOauth, OpenAiAuthMode::CodexApiKey, OpenAiAuthMode::Env,
                                      OpenAiAuthMode::Settings, OpenAiAuthMode::Cliproxy}) {
        if (name == openAiAuthModeName(mode)) {
            return mode;
        }
    }
    return OpenAiAuthMode::Auto;
}

// Where Anthropic sign-ins come from: Claude Code's credentials, or a CLI
// Proxy API account.
enum class AnthropicAuthMode {
    Oauth,
    Cliproxy,
};

inline QString anthropicAuthModeName(AnthropicAuthMode mode)
{
    return mode == AnthropicAuthMode::Cliproxy ? QStringLiteral("cliproxy")
                                               : QStringLiteral("oauth");
}

// Anything unrecognised reads as Oauth.
inline AnthropicAuthMode anthropicAuthModeFromName(const QString &name)
{
    return name == QStringLiteral("cliproxy") ? AnthropicAuthMode::Cliproxy
                                              : AnthropicAuthMode::Oauth;
}

// One Vocabulary Entry. A spelling (no replacement) is a term Speecher keeps
// spelled its way: the speech service may hear it as a hint, and refinement
// reads it. A replacement is a spoken phrase and the exact text written in
// its place, snippets included. A Learned Correction is a replacement
// Speecher added itself from the person's edits, source "learned", and keeps
// what it was learned from. Any entry may be limited to some Writing
// Profiles and some Apps; vocabularyEntryApplies is the one test.
struct VocabularyEntry {
    // A spelling's term, or a replacement's spoken phrase.
    QString term;
    // Where it came from: manual, csv (a terms file), legacy (stored before
    // entries were records), import (a snippets file) or learned.
    QString source = QStringLiteral("manual");
    // First in line for the speech service among key terms. No longer shown
    // or edited; a term starred before keeps its place.
    bool starred = false;
    int frequency = 0;
    qint64 lastUsedMs = 0;
    // What the term means and when it applies, for refinement. Empty for none.
    QString context;
    // The Writing Profiles it is limited to, by id. Empty for every one.
    QStringList profiles;
    // A spelling sent to the speech service as a hint. Refinement uses every
    // spelling either way.
    bool keyTerm = true;
    // The text written in place of the phrase. Empty for a spelling.
    QString replacement;
    // The apps it is limited to: an App's id ("app:…"), or the application id
    // a Learned Correction was learned in. Empty for every app.
    QStringList apps;
    // Stable across edits, so the list can find an entry whose text changed.
    QString id;
    // Off, it applies nowhere and stays listed.
    bool enabled = true;
    // What a Learned Correction was learned from: how sure the edit made
    // Speecher, how many edits showed it, and when.
    double confidence = 0.0;
    int evidenceCount = 0;
    qint64 createdAtMs = 0;
    qint64 lastObservedAtMs = 0;

    bool isReplacement() const { return !replacement.isEmpty(); }
    // A Learned Correction. A spelling an older release learned keeps its
    // source and is a spelling like any other.
    bool isLearned() const { return isReplacement() && source == QStringLiteral("learned"); }

    bool operator==(const VocabularyEntry &other) const = default;
};

struct UiSettings {
    int previewWords = 7;
    bool transcriptionPreviewEnabled = true;
    bool refinementPreviewEnabled = true;
    // A selection edit waits for Replace before it replaces the selection.
    bool selectionEditReviewEnabled = true;
    QString theme = QStringLiteral("system");
    // The UI's colours: one of its own schemes by id, "system" for the
    // desktop's, or "installed:<id>" for a desktop scheme by its file name.
    QString colorScheme = QStringLiteral("steno");
    // The window's side shows the desktop through a blur, where it can.
    bool translucency = true;
    // "framed": the page on a sheet inset in the window; "split": the page
    // beside the sidebar, edge to edge.
    QString windowLayout = QStringLiteral("framed");
    // The window draws its own corners rounded rather than square.
    bool roundedCorners = true;
    // The dictation popup shows the voice as level bars beside the record
    // dot rather than as the scatter ribbon.
    bool boringMode = false;
    bool pauseMediaDuringTranscription = true;
    bool soundsEnabled = false;

    bool operator==(const UiSettings &other) const = default;
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

// A Speed Test result's key: the model, and where it ran.
inline QString localSpeedTestKey(const QString &modelId, const LocalRunsOn &runsOn)
{
    return modelId + QLatin1Char('/') + localRunsOnId(runsOn);
}

// The Local Model is the "local" speech entry's model (ServiceChoice).
struct LocalSpeechSettings {
    LocalRunsOn runsOn;
    // 0 keeps the model loaded until Speecher quits.
    int idleUnloadMinutes = 10;
    // localSpeedTestKey to the Speed Test's measured seconds for the bundled
    // clip, so a result from one Runs on choice never stands for another.
    QMap<QString, double> speedTestSeconds;
    // Whether the "local" entry's model was chosen. False only for an
    // unchosen default, which the hardware's suggestion replaces; legacy
    // saved model ids are choices.
    bool modelChosen = false;

    bool operator==(const LocalSpeechSettings &other) const = default;
};

// The Speed Test's seconds for a model where Runs on places models now, if it
// has run there.
inline std::optional<double> measuredSpeedTestSeconds(const LocalSpeechSettings &local, const QString &modelId)
{
    const auto it = local.speedTestSeconds.constFind(localSpeedTestKey(modelId, local.runsOn));
    return it == local.speedTestSeconds.cend() ? std::nullopt : std::optional<double>(*it);
}

// One of Your servers: a URL the person runs or rents for one job. Speech
// has one, speaking OpenAI's audio transcriptions API; refinement has one,
// speaking OpenAI Chat Completions or Anthropic Messages. Its model is the
// "endpoint" entry's in that job's Service Order.
struct ServerSettings {
    // Speech: the server's origin, without the path. Refinement: the API base
    // the endpoint paths hang off, e.g. http://localhost:11434/v1.
    QString baseUrl;
    // Speech only. whisper.cpp's server answers on /inference.
    QString path = QStringLiteral("/v1/audio/transcriptions");
    // Refinement only: "openai" (Chat Completions) or "anthropic" (Messages).
    QString format = QStringLiteral("openai");
    // Refinement without one sends the CLI Proxy key while on the proxy's
    // server (refinementEndpointKey).
    QString apiKey;

    bool operator==(const ServerSettings &other) const = default;
};

// The stored Spoken Language that leaves the language to the speech service.
inline constexpr auto kAutomaticSpokenLanguage = "auto";

struct SpeechSettings {
    // The speech services in order, each with its model and, after the
    // first, its own Spoken Language. See ProviderChain.h.
    ServiceOrder services = defaultSpeechServices();
    bool codexFinalRetranscribe = true;
    // The Spoken Language the first service listens for: a language code,
    // or kAutomaticSpokenLanguage.
    QString language = QStringLiteral("en");
    QStringList vocabulary;
    QString claudeEndpointBase;
    QString claudeVoicePath;
    LocalSpeechSettings local;
    ServerSettings server;

    bool operator==(const SpeechSettings &other) const = default;
};

// The Local Model the "local" speech entry runs, a LocalModelCatalog id.
inline QString localModelId(const SpeechSettings &speech)
{
    return speech.services.choice(providerKey(ProviderId::Local)).model;
}
inline QString &localModelId(SpeechSettings &speech)
{
    return speech.services.choice(providerKey(ProviderId::Local)).model;
}

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

// Ultrafast serves only GPT-6 Astra and GPT-6.1 Sol.
inline bool openAiModelSupportsUltrafast(const QString &model)
{
    const QString normalized = model.trimmed().toCaseFolded();
    return normalized.startsWith(QStringLiteral("gpt-6-astra")) || normalized.startsWith(QStringLiteral("gpt-6.1-sol"));
}

// The OpenAI speed a refinement with this model runs at: Ultrafast asks for
// Fast on a model that has no Ultrafast tier.
inline QString effectiveOpenAiSpeed(const QString &speed, const QString &model)
{
    return speed == QStringLiteral("ultrafast") && !openAiModelSupportsUltrafast(model)
        ? QStringLiteral("fast")
        : speed;
}

// Refinement through a Local Runner on This computer. Its cleanup model is
// the "local" refinement entry's.
struct LocalRunnerSettings {
    // "ollama", "lmstudio" or "llama-server"; empty until configured.
    QString runner;

    // Every runner but llama-server, which answers with whichever model it
    // loaded, needs a model named.
    bool lacksModel(const QString &model) const { return model.isEmpty() && runner != QStringLiteral("llama-server"); }

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
    // Clean up my words. Off, no service runs, and the order waits for it to
    // come back on.
    bool enabled = true;
    // The refinement services in order, each with its model, effort and speed.
    ServiceOrder services = defaultRefinementServices();
    QString style = QStringLiteral("balanced");
    ServerSettings server;
    LocalRunnerSettings localRunner;
    QString anthropicEndpointBase = QStringLiteral("https://api.anthropic.com/v1");
    QList<WritingProfileSettings> writingProfiles = defaultWritingProfileSettings();
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

    bool operator==(const RefinementSettings &other) const = default;
};

// How Speecher signs in to ChatGPT and Claude, on the Services page: the
// Anthropic auth mode covers Claude Voice dictation and Anthropic refinement,
// the OpenAI auth mode Codex dictation and OpenAI refinement, so a service's
// speech and refinement share one sign-in. CLI Proxy API is a way to sign in
// to either.
struct AccountSettings {
    OpenAiAuthMode openAiAuthMode = OpenAiAuthMode::Auto;
    AnthropicAuthMode anthropicAuthMode = AnthropicAuthMode::Oauth;
    // The chosen CLI Proxy API Account file of each type, or empty while the
    // directory holds only one.
    QString openAiCliproxyAccount;
    QString anthropicCliproxyAccount;
    // The CLI Proxy API account directory in use, detected when nothing was
    // typed.
    QString cliproxyOauthDir;
    // What the user typed, or empty for automatic detection. Saving
    // cliproxyOauthDir back would pin a detected path forever, so the
    // settings surface edits this one.
    QString cliproxyOauthDirConfigured;
    // The CLI Proxy API server ChatGPT and Claude refinement go through in
    // the cliproxy sign-in, instead of the account files; empty for none.
    QString cliproxyBaseUrl;
    QString cliproxyApiKey;
    QString claudeCredentialsPath;

    bool operator==(const AccountSettings &other) const = default;
};

struct OutputSettings {
    DeliverySteps steps;
    OutputFormat format = OutputFormat::PlainText;
    // The virtual keyboard is set up for Linux's Press step.
    bool ydotoolEnabled = false;
    int completionStatusDurationMs = 500;

    bool operator==(const OutputSettings &other) const = default;
};

struct UpdateSettings {
    static constexpr int minimumCheckIntervalMinutes = 5;
    static constexpr int maximumCheckIntervalMinutes = 30 * 24 * 60;

    UpdateChannel channel = UpdateChannel::Stable;
    bool autoCheck = true;
    bool autoInstall = false;
    int checkIntervalMinutes = 30;
    // The unit a custom interval was given in ("minutes", "hours" or "days"),
    // so 8 hours reads back as 8 hours rather than 480 minutes. Empty while
    // one of the listed frequencies is chosen.
    QString checkIntervalUnit;

    bool operator==(const UpdateSettings &other) const = default;
};

// SettingsCodecs::fields() says which of these are stored. SettingsStore::edit
// refuses a change to any other: a snapshot derives or detects those, or
// their owner writes them. What one run chooses for itself travels apart
// (RunOverrides, SpeechAttemptOptions, RefinementContext).
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
    AppsSettings apps;
    RefinementSettings refinement;
    AccountSettings accounts;
    OutputSettings output;
    UpdateSettings updates;
    ShortcutActivationMode shortcutActivationMode = ShortcutActivationMode::Hybrid;
    QList<VocabularyEntry> vocabulary;
    bool correctionLearningEnabled = true;
    HistorySettings history;
};

} // namespace speecher
