#include "providers/SpeechChain.h"

#include "core/SettingsStore.h"
#include "core/settings/SpokenLanguages.h"
#include "dictation/StartupPreparationRunner.h"
#include "providers/ProviderRegistry.h"

#include <QDebug>

#include <utility>

namespace speecher {
namespace {

using Stage = ProviderAttemptIssue::Stage;

// Dropped streams reopened on one provider before it counts as failed.
constexpr int kReconnectsPerProvider = 2;
int s_stableAttemptMs = 10000;

} // namespace

SpeechChain::SpeechChain(ProviderRegistry *providers, Acquire acquire, QObject *parent)
    : QObject(parent)
    , m_providers(providers)
    , m_acquire(std::move(acquire))
    , m_runner(new StartupPreparationRunner(this))
{
    connect(m_runner, &StartupPreparationRunner::completed, this, &SpeechChain::finishPreparation);
}

void SpeechChain::begin(const QString &primary, const QStringList &fallbacks)
{
    cancel();
    m_providerIds = providerChain(ProviderRole::Speech, primary, fallbacks);
    m_index = 0;
    m_refinerRefreshed = false;
}

const QStringList &SpeechChain::providerIds() const
{
    return m_providerIds;
}

QString SpeechChain::currentId() const
{
    return m_providerIds.value(m_index);
}

bool SpeechChain::fallbackRemains(ProviderFailureKind kind) const
{
    return permitsProviderFallback(kind) && m_index + 1 < m_providerIds.size();
}

void SpeechChain::setOptions(const Options &options)
{
    m_options = options;
}

void SpeechChain::setReachability(Reachability reachability)
{
    m_reachability = reachability;
}

bool SpeechChain::offlineFor(ProviderRole role, const QString &providerId, const AppSettings &settings) const
{
    return m_reachability == Reachability::Offline && needsInternet(role, providerId, settings);
}

void SpeechChain::prepare(const AppSettings &settings, TranscriptRefiner *refiner)
{
    m_settings = settings;
    m_refiner = refiner;
    walk();
}

void SpeechChain::prepareNext(const AppSettings &settings, TranscriptRefiner *refiner)
{
    ++m_index;
    prepare(settings, refiner);
}

void SpeechChain::renewSignIn(SpeechTranscriber *transcriber, const SpeechSettings &speech)
{
    std::optional<SpeechPrepareJob> job = transcriber->createPrepareJob(speech);
    const SpeechPrepareResult prepared = job ? SpeechPrepareResult{true, {}} : transcriber->prepare(speech);
    m_runner->start(0, std::move(job), std::nullopt, prepared);
    m_renewing = true;
}

void SpeechChain::cancel()
{
    m_runner->cancel();
    m_renewing = false;
}

// Keys and sign-ins load off this thread when the provider offers a job for
// it; a provider without one prepares here. The walk is done once it emits
// ready() or exhausted(), which a session may answer by walking again.
void SpeechChain::walk()
{
    for (; m_index < m_providerIds.size(); ++m_index) {
        const QString providerId = m_providerIds.at(m_index);
        SpeechSettings speech = m_settings.speech;
        speech.providerId = providerId;
        // Decided as the Fallbacks row decides it.
        if (m_index > 0 && fallbackSkipsSpokenLanguage(speech, providerId)) {
            emit passedOver(providerId, Stage::LanguageSkipped,
                            {ProviderFailureKind::Unavailable,
                             spokenLanguageProblem(speech, m_providers->speechProviderLabel(providerId))});
            continue;
        }
        if (offlineFor(ProviderRole::Speech, providerId, m_settings)) {
            emit passedOver(providerId, Stage::Prepare, noInternetFailure());
            continue;
        }
        SpeechTranscriber *transcriber = m_acquire(providerId);
        if (!transcriber) {
            emit passedOver(providerId, Stage::Prepare,
                            {ProviderFailureKind::Unavailable,
                             QStringLiteral("Unknown speech provider: %1").arg(providerId)});
            continue;
        }
        if (m_options.streamedFinalsOnly && !transcriber->streamsFinalText(speech)) {
            emit passedOver(providerId, Stage::Prepare,
                            {ProviderFailureKind::Unavailable,
                             QStringLiteral("%1 does not stream final text")
                                 .arg(m_providers->speechProviderLabel(providerId))});
            continue;
        }
        if (SettingsStore::hasUnreadProviderSecrets(m_settings, ProviderRole::Speech, providerId)) {
            m_runner->resolveSecrets(0, ProviderRole::Speech, providerId, m_settings);
            return;
        }

        std::optional<SpeechPrepareJob> speechJob = transcriber->createPrepareJob(speech);
        const bool speechRefreshRequired =
            speechJob ? speechJob->showRefreshIndicator : transcriber->requiresRefresh(speech);
        // The refiner's sign-in renews alongside, so refinement need not wait
        // for it later; offline, refinement passes it over.
        std::optional<RefinementRefreshJob> refinerJob;
        bool refinerRefreshRequired = false;
        const RefinementSettings &refinement = m_settings.refinement;
        if (m_refiner && !m_refinerRefreshed
            && !offlineFor(ProviderRole::Refinement, refinement.providerId, m_settings)) {
            refinerJob = m_refiner->createRefreshJob(refinement);
            refinerRefreshRequired =
                refinerJob ? refinerJob->showRefreshIndicator : m_refiner->requiresRefresh(refinement);
        }
        if (speechRefreshRequired || refinerRefreshRequired) {
            emit renewingSignIn();
        }
        const bool renewRefinerHere = !refinerJob && refinerRefreshRequired;
        if (renewRefinerHere && m_options.renewRefinerFirst) {
            renewRefiner();
        }

        SpeechPrepareResult speechPrepared{true, {}};
        if (!speechJob) {
            speechPrepared = transcriber->prepare(speech);
            if (!speechPrepared.ok) {
                emit passedOver(providerId, Stage::Prepare,
                                {speechPrepared.kind, speechPrepared.message, speechPrepared.httpStatus});
                continue;
            }
        }
        if (renewRefinerHere && !m_options.renewRefinerFirst) {
            renewRefiner();
        }
        if (speechJob || refinerJob || m_options.readyLater) {
            m_runner->start(0, std::move(speechJob), std::move(refinerJob), speechPrepared);
            return;
        }
        emit ready();
        return;
    }
    emit exhausted();
}

void SpeechChain::renewRefiner()
{
    m_refiner->refresh(m_settings.refinement);
    m_refinerRefreshed = true;
}

void SpeechChain::finishPreparation(const StartupPreparationResult &result)
{
    if (std::exchange(m_renewing, false)) {
        emit signInRenewed(result.speech);
        return;
    }
    if (!result.speech.ok) {
        emit passedOver(currentId(), Stage::Prepare,
                        {result.speech.kind, result.speech.message, result.speech.httpStatus});
        ++m_index;
        walk();
        return;
    }
    if (result.resolvedSettings) {
        m_settings = *result.resolvedSettings;
        emit secretsResolved(m_settings);
        walk();
        return;
    }
    m_refinerRefreshed |= result.refinerRefreshAttempted;
    if (result.refinerRefreshAttempted && !result.refinerRefresh.ok) {
        qWarning().noquote() << "refinement oauth refresh unavailable status=" + result.refinerRefresh.message;
    }
    emit ready();
}

int SpeechAttemptPolicy::stableAttemptMs()
{
    return s_stableAttemptMs;
}

void SpeechAttemptPolicy::setStableAttemptMs(int ms)
{
    s_stableAttemptMs = ms;
}

SpeechFailure SpeechAttemptPolicy::endedTooSoon(quint64 attemptId)
{
    return {attemptId, QStringLiteral("The speech stream ended within seconds of starting"), true,
            SpeechPhase::Streaming, ProviderFailureKind::Network};
}

void SpeechAttemptPolicy::refill()
{
    m_reconnectsLeft = kReconnectsPerProvider;
}

void SpeechAttemptPolicy::refillIfAttemptWasStable()
{
    if (attemptWasStable()) {
        refill();
    }
}

bool SpeechAttemptPolicy::takeReconnect()
{
    if (m_reconnectsLeft == 0) {
        return false;
    }
    --m_reconnectsLeft;
    return true;
}

void SpeechAttemptPolicy::attemptStarted()
{
    m_attemptClock.start();
}

bool SpeechAttemptPolicy::attemptWasStable() const
{
    return m_attemptClock.isValid() && m_attemptClock.elapsed() >= s_stableAttemptMs;
}

} // namespace speecher
