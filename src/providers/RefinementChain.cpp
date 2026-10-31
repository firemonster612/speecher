#include "providers/RefinementChain.h"

#include "core/SettingsStore.h"
#include "providers/PreparationRunner.h"

#include <utility>

namespace speecher {
namespace {

using Stage = ProviderAttemptIssue::Stage;

} // namespace

RefinementChain::RefinementChain(Acquire acquire, QObject *parent)
    : QObject(parent)
    , m_acquire(std::move(acquire))
    , m_runner(new PreparationRunner(this))
{
    connect(m_runner, &PreparationRunner::refinerPrepared, this, &RefinementChain::finishPreparation);
    connect(m_runner, &PreparationRunner::secretsResolved, this, &RefinementChain::useResolvedSecrets);
}

void RefinementChain::begin(const QString &primary, const QStringList &fallbacks)
{
    cancel();
    m_providerIds = providerChain(ProviderRole::Refinement, primary, fallbacks);
    m_index = 0;
}

const QStringList &RefinementChain::providerIds() const
{
    return m_providerIds;
}

QString RefinementChain::currentId() const
{
    return m_providerIds.value(m_index);
}

RefinementSettings RefinementChain::currentSettings() const
{
    RefinementSettings refinement = m_refinement;
    refinement.providerId = currentId();
    refinement.endpoint.apiKey = m_settings.refinement.endpoint.apiKey;
    refinement.cliproxyApiKey = m_settings.refinement.cliproxyApiKey;
    return refinement;
}

void RefinementChain::setReachability(Reachability reachability)
{
    m_reachability = reachability;
}

void RefinementChain::prepare(const AppSettings &settings, const RefinementSettings &refinement)
{
    ++m_walkGeneration;
    m_settings = settings;
    m_refinement = refinement;
    walk();
}

void RefinementChain::prepareNext(const AppSettings &settings, const RefinementSettings &refinement)
{
    ++m_index;
    prepare(settings, refinement);
}

void RefinementChain::cancel()
{
    ++m_walkGeneration;
    m_runner->cancel();
}

// Keys and sign-ins load off this thread when the refiner needs them; the rest
// of its preparation runs here. The walk is done once it emits ready() or
// exhausted(), which a session may answer by walking again.
void RefinementChain::walk()
{
    const quint64 generation = m_walkGeneration;
    for (; m_index < m_providerIds.size(); ++m_index) {
        const QString providerId = m_providerIds.at(m_index);
        if (m_reachability == Reachability::Offline && needsInternet(ProviderRole::Refinement, providerId, m_settings)) {
            emit passedOver(providerId, Stage::Prepare, noInternetFailure());
            if (generation != m_walkGeneration) {
                return;
            }
            continue;
        }
        m_refiner = m_acquire(providerId);
        if (!m_refiner) {
            emit passedOver(providerId, Stage::Prepare,
                            {ProviderFailureKind::Unavailable,
                             QStringLiteral("Unknown refinement provider: %1").arg(providerId)});
            if (generation != m_walkGeneration) {
                return;
            }
            continue;
        }
        if (SettingsStore::hasUnreadProviderSecrets(m_settings, ProviderRole::Refinement, providerId)) {
            // Started first: a handler of preparing() that cancels the walk
            // cancels the job with it.
            m_runner->resolveSecrets(ProviderRole::Refinement, providerId, m_settings);
            emit preparing();
            return;
        }
        if (std::optional<PreparationJob> refresh = m_refiner->createRefreshJob(currentSettings(), m_settings.accounts)) {
            m_runner->prepareRefiner(std::move(*refresh));
            emit preparing();
            return;
        }
        prepareCurrent();
        return;
    }
    emit exhausted();
}

// What is left of the current refiner's preparation once its keys and
// sign-in are in.
void RefinementChain::prepareCurrent()
{
    const std::optional<ProviderFailure> failure = m_refiner->prepare(currentSettings(), m_settings.accounts);
    if (failure) {
        passOver(*failure);
        return;
    }
    emit ready();
}

void RefinementChain::passOver(const ProviderFailure &failure)
{
    const quint64 generation = m_walkGeneration;
    emit passedOver(currentId(), Stage::Prepare, failure);
    if (generation != m_walkGeneration) {
        return;
    }
    ++m_index;
    walk();
}

void RefinementChain::finishPreparation(const std::optional<ProviderFailure> &failure)
{
    if (failure) {
        passOver(*failure);
        return;
    }
    prepareCurrent();
}

void RefinementChain::useResolvedSecrets(const std::optional<ProviderFailure> &failure, const AppSettings &settings)
{
    if (failure) {
        passOver(*failure);
        return;
    }
    m_settings = settings;
    const quint64 generation = m_walkGeneration;
    emit secretsResolved(m_settings);
    if (generation != m_walkGeneration) {
        return;
    }
    walk();
}

} // namespace speecher
