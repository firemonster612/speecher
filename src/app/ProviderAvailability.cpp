#include "app/ProviderAvailability.h"

#include "app/NetworkReachability.h"

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

void ProviderAvailability::addTo(LiveFacts &facts) const
{
    facts.reachability = m_reachability.reachability();
    facts.signedIn = m_signedIn;
}

} // namespace speecher
