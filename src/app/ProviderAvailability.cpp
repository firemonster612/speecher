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

void ProviderAvailability::checkSpeechSignIns(ProviderRegistry &registry, const AppSettings &settings)
{
    for (const ProviderDescriptor &descriptor : registry.speechProviders()) {
        if (!providerSignsIn(descriptor.id) || checkingSignIn(descriptor.id)) {
            continue;
        }
        m_checkingSignIns.insert(descriptor.id);
        m_signInCheckFailures.remove(descriptor.id);
        emit changed();
        // An instance of its own, so the check never touches dictation's.
        SpeechTranscriber *provider = registry.createSpeechProvider(descriptor.id, &registry);
        const auto note = [this, id = descriptor.id, provider](const std::optional<ProviderFailure> &failure) {
            provider->deleteLater();
            m_checkingSignIns.remove(id);
            const bool unreached = failure
                && (failure->kind == ProviderFailureKind::Network || failure->kind == ProviderFailureKind::Timeout);
            if (unreached) {
                m_signInCheckFailures.insert(id, failure->message);
            } else {
                noteSignIn(id, !failure);
            }
            emit changed();
        };
        std::optional<PreparationJob> job = provider->createPrepareJob(settings.speech, settings.accounts);
        if (!job || !job->run) {
            note(provider->prepare(settings.speech, settings.accounts));
            continue;
        }
        runProviderProbe<std::optional<ProviderFailure>>(&registry, this, std::move(job->run), note);
    }
}

void ProviderAvailability::addTo(LiveFacts &facts) const
{
    facts.reachability = m_reachability.reachability();
    facts.signedIn = m_signedIn;
}

} // namespace speecher
