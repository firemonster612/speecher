#include "dictation/DictationPlan.h"

#include "core/ProviderChain.h"
#include "core/ProviderTable.h"
#include "core/Vocabulary.h"
#include "core/settings/SpokenLanguages.h"

namespace speecher {

namespace {

const QString kRun = QStringLiteral("run");
const QString kDefault = QStringLiteral("default");

// Where a value resolveApp settled came from, as a plan's source.
QString appSource(AppLayer layer, const AppResolution &app)
{
    switch (layer) {
    case AppLayer::App:
        return app.appId;
    case AppLayer::BuiltIn:
        return QStringLiteral("builtin");
    case AppLayer::Terminal:
        return QStringLiteral("type:") + appCategoryName(AppCategory::Terminal);
    case AppLayer::Type:
        return QStringLiteral("type:") + appCategoryName(app.type);
    case AppLayer::Fallback:
        return kDefault;
    }
    return kDefault;
}

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

// The order a run walks once it names run's service: led by it, over the
// order its wider layer gave when that leads with the same service, so the
// profile's model stays, and over the page's order otherwise.
ServiceOrder runLed(const ServiceOrder &wider, const ServiceOrder &page, const ServiceChoice &run)
{
    ServiceOrder led = (wider.main() == run.service ? wider : page).withMain(run.service);
    led.choice(run.service) = mergedServiceChoice(led.choice(run.service), run);
    return led;
}

} // namespace

DictationPlan planDictation(const AppSettings &saved,
                            const Target &target,
                            const RunOverrides &run,
                            const OffersSpeech &offersSpeech)
{
    DictationPlan plan;
    plan.settings = saved;
    AppSettings &settings = plan.settings;
    QHash<QString, QString> &sources = plan.sources;
    plan.app = resolveApp(target, saved.apps);

    // The Writing Profile, then what the run changes of it.
    const QString profileId = run.writingProfile.value_or(plan.app.writingProfile);
    sources.insert(QStringLiteral("writingProfile"),
                   run.writingProfile ? kRun : appSource(plan.app.writingProfileFrom, plan.app));
    plan.profile = writingProfileSettingsFor(saved.refinement.writingProfiles, profileId);
    plan.profile.profile = profileId;
    const QString fromProfile = QStringLiteral("profile:") + profileId;
    if (run.cleanupLevel) {
        plan.profile.cleanupStrength = *run.cleanupLevel;
    }
    if (run.tone) {
        plan.profile.tone = *run.tone;
    }
    sources.insert(QStringLiteral("cleanup"), run.cleanupLevel ? kRun : fromProfile);
    sources.insert(QStringLiteral("tone"), run.tone ? kRun : fromProfile);
    sources.insert(QStringLiteral("outputLanguage"), fromProfile);
    sources.insert(QStringLiteral("instructions"), fromProfile);

    sources.insert(QStringLiteral("pasting"), appSource(plan.app.pastingFrom, plan.app));
    if (run.outputFormat) {
        settings.output.format = *run.outputFormat;
    }
    sources.insert(QStringLiteral("outputFormat"), run.outputFormat ? kRun : kDefault);

    if (run.spokenLanguage) {
        settings.speech.language = *run.spokenLanguage;
    }
    sources.insert(QStringLiteral("spokenLanguage"), run.spokenLanguage ? kRun : kDefault);

    // Speech: the profile's first service, where this build has it and it
    // listens for the run's language, then the run's.
    const SpeechSettings speechPage = settings.speech;
    QString speechFrom = kDefault;
    if (const std::optional<ServiceOrder> led = ledBy(ProviderRole::Speech, speechPage.services, plan.profile.speechFirst)) {
        SpeechSettings profiled = speechPage;
        profiled.services = *led;
        if (listensForSpokenLanguage(profiled) && (!offersSpeech || offersSpeech(led->main()))) {
            settings.speech = profiled;
            speechFrom = fromProfile;
        }
    }
    if (run.speech) {
        settings.speech.services = runLed(settings.speech.services, speechPage.services, *run.speech);
        speechFrom = kRun;
    }
    sources.insert(QStringLiteral("speech"), speechFrom);

    // Refinement: the profile's first service, then the run's, which refines
    // whatever Clean up my words says, or not at all.
    RefinementSettings &refinement = settings.refinement;
    const ServiceOrder refinementPage = refinement.services;
    QString refinementFrom = kDefault;
    if (const std::optional<ServiceOrder> led =
            ledBy(ProviderRole::Refinement, refinementPage, plan.profile.refinementFirst)) {
        refinement.services = *led;
        refinementFrom = fromProfile;
    }
    if (run.refinement) {
        refinement.enabled = run.refinement->service != kNoRefinementProvider;
        if (refinement.enabled) {
            refinement.services = runLed(refinement.services, refinementPage, *run.refinement);
        }
        refinementFrom = kRun;
    }
    sources.insert(QStringLiteral("refinement"), refinementFrom);
    refinement.style = refinedCleanupLevel(plan.profile.cleanupStrength, plan.profile.outputLanguage);

    // The vocabulary: the saved lists unless the run leaves them out, and
    // the run's own terms ahead of them.
    if (!run.applyVocabulary) {
        settings.vocabulary.clear();
        settings.learnedCorrections.clear();
        settings.bindings.clear();
    }
    plan.addedVocabulary = run.addedVocabulary;
    settings.speech.vocabulary =
        speechVocabulary(settings.vocabulary, settings.learnedCorrections, profileId, run.addedVocabulary);
    sources.insert(QStringLiteral("vocabulary"),
                   !run.applyVocabulary || !run.addedVocabulary.isEmpty() ? kRun : kDefault);
    return plan;
}

} // namespace speecher
