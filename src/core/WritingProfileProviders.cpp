#include "core/WritingProfileProviders.h"

#include "core/ProviderChain.h"
#include "core/settings/SpokenLanguages.h"

namespace speecher {

namespace {

// The page's whole chain follows a profile's service, the page's primary
// first, without the profile's service a second time.
QStringList pageChainAfter(const QString &profileProvider, QStringList pageChain)
{
    pageChain.removeAll(profileProvider);
    return pageChain;
}

} // namespace

bool profilePicksSpeechModel(const QString &providerId)
{
    return providerId == QStringLiteral("local");
}

bool profilePicksRefinementModel(const QString &providerId)
{
    return providerId == QStringLiteral("openai") || providerId == QStringLiteral("anthropic");
}

std::optional<SpeechSettings> profileSpeechSettings(const SpeechSettings &speech,
                                                    const WritingProfileSettings &profile)
{
    if (!isChainProviderId(ProviderRole::Speech, profile.speechProvider)) {
        return std::nullopt;
    }
    SpeechSettings chosen = speech;
    chosen.providerId = profile.speechProvider;
    if (profilePicksSpeechModel(chosen.providerId) && !profile.speechModel.isEmpty()) {
        chosen.local.modelId = profile.speechModel;
    }
    chosen.fallbackProviderIds = pageChainAfter(
        chosen.providerId, providerChain(ProviderRole::Speech, speech.providerId, speech.fallbackProviderIds));
    return chosen;
}

AppSettings withWritingProfileProviders(AppSettings settings, const WritingProfileSettings &profile)
{
    if (const std::optional<SpeechSettings> speech = profileSpeechSettings(settings.speech, profile);
        speech && listensForSpokenLanguage(*speech)) {
        settings.speech = *speech;
    }
    RefinementSettings &refinement = settings.refinement;
    if (isChainProviderId(ProviderRole::Refinement, profile.refinementProvider)) {
        const QStringList pageChain =
            providerChain(ProviderRole::Refinement, refinement.providerId, refinement.fallbackProviderIds);
        refinement.providerId = profile.refinementProvider;
        refinement.fallbackProviderIds = pageChainAfter(refinement.providerId, pageChain);
        if (!profile.refinementModel.isEmpty()) {
            if (refinement.providerId == QStringLiteral("openai")) {
                refinement.openAiModel = profile.refinementModel;
            } else if (refinement.providerId == QStringLiteral("anthropic")) {
                refinement.anthropicModel = profile.refinementModel;
            }
        }
    }
    return settings;
}

} // namespace speecher
