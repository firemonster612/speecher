#include "app/ProviderSetup.h"

#include "providers/AnthropicTranscriptRefiner.h"
#include "providers/ClaudeSpeechTranscriber.h"
#include "providers/CodexSpeechTranscriber.h"
#ifdef SPEECHER_E2E_HOOKS
#include "providers/E2EProviders.h"
#endif
#include "providers/OpenAiTranscriptRefiner.h"
#include "providers/ProviderRegistry.h"

namespace speecher {
namespace {

// Model names follow the September 22, 2026 defaults. The speed and quality
// lines were measured on the previous defaults (gpt-5.6-luna at effort none,
// claude-sonnet-4-6 at effort low; see .scratch/provider-stats/FINDINGS.md)
// and are estimated forward from vendor latency notes until the new defaults
// are benchmarked. The score is the maintainers' overall ranking, folding
// those lines into one number out of 10.
QVector<ProviderStat> refinementProviderStats(const QString &id)
{
    if (id == QStringLiteral("openai")) {
        return {{QStringLiteral("Score"), QStringLiteral("9 / 10")},
                {QStringLiteral("Default model"), QStringLiteral("gpt-6-luna")},
                {QStringLiteral("Speed"), QStringLiteral("About 3 seconds per dictation (estimated)")},
                {QStringLiteral("Efficiency"), QStringLiteral("No reasoning pass; time varies run to run")},
                {QStringLiteral("Quality"), QStringLiteral("Excellent cleanup; applies spoken corrections reliably")}};
    }
    if (id == QStringLiteral("anthropic")) {
        return {{QStringLiteral("Score"), QStringLiteral("8 / 10")},
                {QStringLiteral("Default model"), QStringLiteral("Claude Opus 5.5")},
                {QStringLiteral("Speed"), QStringLiteral("About 3 seconds per dictation (estimated)")},
                {QStringLiteral("Efficiency"), QStringLiteral("Always reasons, kept light at low effort")},
                {QStringLiteral("Quality"), QStringLiteral("Excellent cleanup; can leave a spoken correction in")}};
    }
    return {};
}

} // namespace

void registerProviders(ProviderRegistry &registry, SecretStore *secrets)
{
#ifdef SPEECHER_E2E_HOOKS
    // E2E-build-only hook: deterministic stub providers for the headless
    // dictation-panel flow runs. Never compiled into distributed builds.
    if (qEnvironmentVariableIntValue("SPEECHER_E2E_STUB") == 1) {
        // The stub stats mirror the real providers' shape (a Score line first)
        // so the setup-flow E2E can assert the rendering.
        registry.registerSpeechProvider(
            {QStringLiteral("e2e-stub"), QStringLiteral("E2E stub"), QString(), false, QString(),
             {{QStringLiteral("Score"), QStringLiteral("8 / 10")},
              {QStringLiteral("Engine"), QStringLiteral("Deterministic test stub")}}},
            createE2ESpeechTranscriber);
        registry.registerRefinementProvider(
            {QStringLiteral("e2e-stub"), QStringLiteral("E2E stub"), QString(), false, QString(),
             {{QStringLiteral("Score"), QStringLiteral("8 / 10")},
              {QStringLiteral("Engine"), QStringLiteral("Deterministic test stub")}}},
            createE2ETranscriptRefiner);
    }
#endif
    registry.registerSpeechProvider(
        {QStringLiteral("claude"),
         QStringLiteral("Claude Voice"),
         QStringLiteral("Install Claude Code from claude.com/code and sign in — the desktop app or the claude CLI (/login) both work."),
         false,
         QStringLiteral("Deepgram Nova 3: words appear live as you speak. "
                        "About 60 languages, automatic punctuation and numerals."),
         {{QStringLiteral("Score"), QStringLiteral("8 / 10")},
          {QStringLiteral("Engine"), QStringLiteral("Deepgram Nova 3")},
          {QStringLiteral("Languages"), QStringLiteral("About 60")},
          {QStringLiteral("Speed"), QStringLiteral("Live stream; words appear as you speak")},
          {QStringLiteral("Accuracy"), QStringLiteral("Strong, holds up in noisy rooms")},
          {QStringLiteral("Formatting"), QStringLiteral("Automatic punctuation, capitals, numerals")}}},
        [](QObject *parent) {
            return new ClaudeSpeechTranscriber(parent);
        });
    registry.registerSpeechProvider(
        {QStringLiteral("codex"),
         QStringLiteral("ChatGPT Codex"),
         QStringLiteral("Sign in with ChatGPT in the ChatGPT app, or install the Codex CLI and run codex login."),
         false,
         QStringLiteral("GPT Live Transcribe: very accurate; text arrives a phrase "
                        "at a time after short pauses. Around 100 languages."),
         {{QStringLiteral("Score"), QStringLiteral("9 / 10")},
          {QStringLiteral("Engine"), QStringLiteral("GPT Live Transcribe")},
          {QStringLiteral("Languages"), QStringLiteral("Around 100")},
          {QStringLiteral("Speed"), QStringLiteral("A phrase at a time, after a short pause")},
          {QStringLiteral("Accuracy"), QStringLiteral("Excellent, even with accents and noise")},
          {QStringLiteral("Formatting"), QStringLiteral("Natural punctuation and phrasing")}}},
        [](QObject *parent) {
            return new CodexSpeechTranscriber(parent);
        });
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
}

} // namespace speecher
