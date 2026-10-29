#include "app/ProviderSetup.h"

#include "core/ProviderTable.h"

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

void registerProviders(ProviderRegistry &registry, SecretStore *secrets, const LocalModelStore *localModels)
{
#ifdef SPEECHER_E2E_HOOKS
    // E2E-build-only hook: deterministic stub providers for the headless
    // dictation-panel flow runs. Never compiled into distributed builds.
    if (qEnvironmentVariableIntValue("SPEECHER_E2E_STUB") == 1) {
        // The stubs have no rating; the setup-flow E2E asserts the real
        // providers' bars beside them.
        registry.registerSpeechProvider({QStringLiteral("e2e-stub"), QStringLiteral("E2E stub")},
                                        createE2ESpeechTranscriber);
        registry.registerRefinementProvider({QStringLiteral("e2e-stub"), QStringLiteral("E2E stub")},
                                            createE2ETranscriptRefiner);
    }
#endif
    registry.registerSpeechProvider(
        {providerKey(ProviderId::Claude), QStringLiteral("Claude")},
        [](QObject *parent) {
            return new ClaudeSpeechTranscriber(parent);
        });
    registry.registerSpeechProvider(
        {providerKey(ProviderId::Codex), QStringLiteral("ChatGPT")},
        [](QObject *parent) {
            return new CodexSpeechTranscriber(parent);
        });
#ifdef SPEECHER_WITH_LOCAL_SPEECH
    registry.registerSpeechProvider(
        {providerKey(ProviderId::Local), QStringLiteral("Local Model")},
        [localModels](QObject *parent) {
            return new LocalSpeechTranscriber(*localModels, parent);
        });
#else
    Q_UNUSED(localModels);
#endif
    registry.registerRefinementProvider(
        {providerKey(ProviderId::OpenAi), QStringLiteral("ChatGPT"), true},
        [secrets](QObject *parent) { return new OpenAiTranscriptRefiner(secrets, parent); });
    registry.registerRefinementProvider(
        {providerKey(ProviderId::Anthropic), QStringLiteral("Claude"), true},
        [](QObject *parent) { return new AnthropicTranscriptRefiner(parent); });
    registry.registerSpeechProvider(
        {providerKey(ProviderId::Endpoint), QStringLiteral("Custom Endpoint")},
        [](QObject *parent) { return new EndpointSpeechTranscriber(parent); });
    registry.registerRefinementProvider(
        {providerKey(ProviderId::Endpoint), QStringLiteral("Custom Endpoint")},
        [](QObject *parent) { return new EndpointTranscriptRefiner(parent); });
    registry.registerRefinementProvider(
        {providerKey(ProviderId::Local), QStringLiteral("Local Runner")},
        [](QObject *parent) { return new LocalRunnerRefiner(parent); });
}

} // namespace speecher
