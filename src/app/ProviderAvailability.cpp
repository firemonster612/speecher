#include "app/ProviderAvailability.h"

#include "app/NetworkReachability.h"
#include "providers/ProviderProbe.h"
#include "providers/ProviderRegistry.h"

namespace speecher {

ProviderAvailability::ProviderAvailability(const NetworkReachability &reachability, QObject *parent)
    : QObject(parent)
    , m_reachability(reachability)
{
    connect(&reachability, &NetworkReachability::changed, this, &ProviderAvailability::changed);
}

void ProviderAvailability::noteSignIn(const QString &providerId, bool signedIn)
{
    if (m_signedIn.value(providerId, !signedIn) == signedIn) {
        return;
    }
    m_signedIn.insert(providerId, signedIn);
    emit changed();
}

void ProviderAvailability::checkSpeechSignIns(ProviderRegistry &registry, const SpeechSettings &speech)
{
    for (const ProviderDescriptor &descriptor : registry.speechProviders()) {
        if (!providerSignsIn(descriptor.id)) {
            continue;
        }
        // An instance of its own, so the check never touches dictation's.
        SpeechTranscriber *provider = registry.createSpeechProvider(descriptor.id, &registry);
        const auto note = [this, id = descriptor.id, provider](const SpeechPrepareResult &result) {
            provider->deleteLater();
            const bool unreached =
                result.kind == ProviderFailureKind::Network || result.kind == ProviderFailureKind::Timeout;
            if (result.ok || !unreached) {
                noteSignIn(id, result.ok);
            }
        };
        std::optional<SpeechPrepareJob> job = provider->createPrepareJob(speech);
        if (!job || !job->run) {
            note(provider->prepare(speech));
            continue;
        }
        runProviderProbe<SpeechPrepareResult>(&registry, this, std::move(job->run), note);
    }
}

void ProviderAvailability::addTo(LiveFacts &facts) const
{
    facts.reachability = m_reachability.reachability();
    facts.signedIn = m_signedIn;
}

} // namespace speecher
