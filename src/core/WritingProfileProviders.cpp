#include "core/WritingProfileProviders.h"

#include "core/ProviderChain.h"
#include "core/settings/SpokenLanguages.h"

namespace speecher {

namespace {

// The order with first's service leading it, running first's choice over
// the order's own entry for that service, and the order's services after it.
// Nullopt for a choice that names no service of the role.
std::optional<ServiceOrder> ledBy(ProviderRole role, const ServiceOrder &services,
                                  const std::optional<ServiceChoice> &first)
{
    if (!first || !isChainProviderId(role, first->service)) {
        return std::nullopt;
    }
    ServiceOrder led = services;
    led.order = serviceChain(role, services.order);
    led = led.withMain(first->service);
    led.choice(first->service) = mergedServiceChoice(services.choice(first->service), *first);
    return led;
}

} // namespace

std::optional<SpeechSettings> profileSpeechSettings(const SpeechSettings &speech,
                                                    const WritingProfileSettings &profile)
{
    const std::optional<ServiceOrder> services = ledBy(ProviderRole::Speech, speech.services, profile.speechFirst);
    if (!services) {
        return std::nullopt;
    }
    SpeechSettings chosen = speech;
    chosen.services = *services;
    return chosen;
}

AppSettings withWritingProfileProviders(AppSettings settings, const WritingProfileSettings &profile)
{
    if (const std::optional<SpeechSettings> speech = profileSpeechSettings(settings.speech, profile);
        speech && listensForSpokenLanguage(*speech)) {
        settings.speech = *speech;
    }
    RefinementSettings &refinement = settings.refinement;
    if (const std::optional<ServiceOrder> services =
            ledBy(ProviderRole::Refinement, refinement.services, profile.refinementFirst)) {
        refinement.services = *services;
    }
    return settings;
}

} // namespace speecher
