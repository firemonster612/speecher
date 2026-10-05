#include "app/ProviderSetup.h"
#include "core/LocalModelCatalog.h"
#include "core/settings/SettingsSchema.h"

#include "providers/AnthropicTranscriptRefiner.h"
#include "providers/ClaudeSpeechTranscriber.h"
#include "providers/CodexSpeechTranscriber.h"
#ifdef SPEECHER_E2E_HOOKS
#include "providers/E2EProviders.h"
#endif
#include "providers/EndpointSpeechTranscriber.h"
#include "providers/EndpointTranscriptRefiner.h"
#include "providers/LocalRunner.h"
#ifdef SPEECHER_WITH_LOCAL_SPEECH
#include "providers/LocalSpeechTranscriber.h"
#endif
#include "providers/OpenAiTranscriptRefiner.h"
#include "providers/ProviderRegistry.h"

namespace speecher {
namespace {

// Speed, accuracy and quality are rating bars (core/settings/ProviderRatings),
// and the models behind a provider are in its Advanced list, so the stats
// keep only what those leave out.
QVector<ProviderStat> refinementProviderStats(const QString &id)
{
    if (id == QStringLiteral("openai")) {
        return {{QStringLiteral("Efficiency"), QStringLiteral("No reasoning pass; time varies run to run")}};
    }
    if (id == QStringLiteral("anthropic")) {
        return {{QStringLiteral("Efficiency"), QStringLiteral("Always reasons, kept light at low effort")}};
    }
    return {};
}

ProviderStat textShows(const QString &value)
{
    return {LocalModelFactLabels{}.textShows, value};
}

} // namespace

void registerProviders(ProviderRegistry &registry, SecretStore *secrets, const LocalModelStore *localModels)
{
#ifdef SPEECHER_E2E_HOOKS
    // E2E-build-only hook: deterministic stub providers for the headless
    // dictation-panel flow runs. Never compiled into distributed builds.
    if (qEnvironmentVariableIntValue("SPEECHER_E2E_STUB") == 1) {
        // The stubs have no rating; the setup-flow E2E asserts the real
        // providers' bars beside them.
        registry.registerSpeechProvider(
            {QStringLiteral("e2e-stub"), QStringLiteral("E2E stub"), QString(), false, QString(),
             {textShows(QStringLiteral("Deterministic test stub"))}},
            createE2ESpeechTranscriber);
        registry.registerRefinementProvider(
            {QStringLiteral("e2e-stub"), QStringLiteral("E2E stub"), QString(), false, QString(),
             {{QStringLiteral("Model"), QStringLiteral("Deterministic test stub")}}},
            createE2ETranscriptRefiner);
    }
#endif
    registry.registerSpeechProvider(
        {QStringLiteral("claude"),
         QStringLiteral("Claude Voice"),
         QStringLiteral("Install Claude Code from claude.com/code and sign in, in the desktop app or "
                        "with /login in the claude CLI."),
         false,
         QStringLiteral("About 60 languages, automatic punctuation and numerals."),
         {{QStringLiteral("Languages"), QStringLiteral("About 60")},
          textShows(QStringLiteral("Live stream; words appear as you speak")),
          {QStringLiteral("Formatting"), QStringLiteral("Automatic punctuation, capitals, numerals")}}},
        [](QObject *parent) {
            return new ClaudeSpeechTranscriber(parent);
        });
    registry.registerSpeechProvider(
        {QStringLiteral("codex"),
         QStringLiteral("ChatGPT Codex"),
         QStringLiteral("Sign in with ChatGPT in the ChatGPT app, or install the Codex CLI and run codex login."),
         false,
         QStringLiteral("Very accurate, around 100 languages."),
         {{QStringLiteral("Languages"), QStringLiteral("Around 100")},
          textShows(QStringLiteral("A phrase at a time, after a short pause")),
          {QStringLiteral("Formatting"), QStringLiteral("Natural punctuation and phrasing")}}},
        [](QObject *parent) {
            return new CodexSpeechTranscriber(parent);
        });
#ifdef SPEECHER_WITH_LOCAL_SPEECH
    registry.registerSpeechProvider(
        {QStringLiteral("local"),
         QStringLiteral("Local Model"),
         QStringLiteral("Download a model on the %1 page. It runs on this computer, with no account.")
             .arg(paneTitle(QStringLiteral("localModels"))),
         false,
         QStringLiteral("Runs on this computer: no account, works offline after a one-time "
                        "download. Languages depend on the model; speed on the model and this computer."),
         {{QStringLiteral("Languages"), QStringLiteral("Depend on the model")}}},
        [localModels](QObject *parent) {
            return new LocalSpeechTranscriber(*localModels, parent);
        });
#else
    Q_UNUSED(localModels);
#endif
    registry.registerRefinementProvider(
        {QStringLiteral("openai"), QStringLiteral("OpenAI"),
         QStringLiteral("Uses your ChatGPT or Codex sign-in."), true,
         QString(), refinementProviderStats(QStringLiteral("openai"))},
        [secrets](QObject *parent) { return new OpenAiTranscriptRefiner(secrets, parent); });
    registry.registerRefinementProvider(
        {QStringLiteral("anthropic"), QStringLiteral("Anthropic"),
         QStringLiteral("Uses your Claude Code sign-in."), true,
         QString(), refinementProviderStats(QStringLiteral("anthropic"))},
        [](QObject *parent) { return new AnthropicTranscriptRefiner(parent); });
    registry.registerSpeechProvider(
        {QStringLiteral("endpoint"),
         QStringLiteral("Custom Endpoint"),
         // No setup hint: the assistants show the endpoint's fields instead,
         // each with its settings row's help.
         QString(),
         false,
         QStringLiteral("A server you run. Speed, accuracy and languages depend on the server "
                        "and its model."),
         {textShows(textShowsValue(false)),
          {QStringLiteral("Formatting"), QStringLiteral("Whatever the server returns")}}},
        [](QObject *parent) { return new EndpointSpeechTranscriber(parent); });
    registry.registerRefinementProvider(
        {QStringLiteral("endpoint"), QStringLiteral("Custom Endpoint"),
         QStringLiteral("A server you run, or CLI Proxy API, with an OpenAI- or Anthropic-compatible API."),
         false, QString(),
         {{QStringLiteral("Model"), QStringLiteral("Any model your server offers")}}},
        [](QObject *parent) { return new EndpointTranscriptRefiner(parent); });
    registry.registerRefinementProvider(
        {QStringLiteral("local"), QStringLiteral("Local Runner"),
         QStringLiteral("Runs on this computer through Ollama, LM Studio or llama-server."),
         false, QString(),
         {{QStringLiteral("Model"), QStringLiteral("A cleanup model in your Local Runner")},
          {QStringLiteral("Privacy"), QStringLiteral("The transcript stays on this computer")}}},
        [](QObject *parent) { return new LocalRunnerRefiner(parent); });
}

} // namespace speecher
