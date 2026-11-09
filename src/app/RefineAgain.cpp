#include "app/RefineAgain.h"

#include "core/ProviderChain.h"
#include "core/SettingsStore.h"
#include "providers/ProviderRegistry.h"
#include "providers/RefinementChain.h"
#include "providers/TranscriptRefiner.h"

namespace speecher {

RefineAgain::RefineAgain(SettingsStore &settings, ProviderRegistry &providers, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_providers(providers)
    , m_chain(new RefinementChain([this](const QString &providerId) { return acquireRefiner(providerId); }, this))
{
    connect(m_chain, &RefinementChain::passedOver, this,
            [this](const QString &, ProviderAttemptIssue::Stage, const ProviderFailure &failure) {
                m_lastFailure = failure.message;
            });
    connect(m_chain, &RefinementChain::secretsResolved, this,
            [this](const AppSettings &settings) { m_plan.settings = settings; });
    connect(m_chain, &RefinementChain::ready, this, [this] {
        m_refiner->refine(m_pipeline.refinementInput, m_pipeline.refinementVocabulary, m_pipeline.refinementContext,
                          m_chain->currentSettings(), m_plan.settings.accounts);
    });
    connect(m_chain, &RefinementChain::exhausted, this, [this] { fail(m_lastFailure); });
}

void RefineAgain::refine(qint64 entryId, const QString &raw, const QString &writingProfile)
{
    m_chain->cancel();
    releaseRefiner();
    m_entryId = entryId;
    m_lastFailure.clear();
    RunOverrides run;
    run.writingProfile = writingProfile;
    const AppSettings saved = m_settings.dictationSnapshot();
    m_plan = planDictation(saved, Target{}, run, m_providers.offersSpeech());
    // Asked for, it refines with Clean up my words off too, with what a
    // dictation would refine with if it were on.
    if (!m_plan.settings.refinement.enabled && !m_plan.settings.refinement.services.main().isEmpty()) {
        run.refinement = ServiceChoice{m_plan.settings.refinement.services.main(), {}};
        m_plan = planDictation(saved, Target{}, run, m_providers.offersSpeech());
    }
    m_pipeline = TranscriptPipeline::prepare(raw, m_plan, Target{}, m_plan.settings.speech.language);
    const QStringList chain = serviceChain(m_plan.settings.refinement);
    if (chain.isEmpty()) {
        emit failed(entryId, QStringLiteral("No refinement service is set up. Add one on Refinement."));
        return;
    }
    if (m_pipeline.refinementSettings.style == QStringLiteral("none")) {
        emit failed(entryId, QStringLiteral("This profile's cleanup is None, so there's nothing to refine."));
        return;
    }
    if (m_pipeline.bindingResult.canSkipRefinement) {
        emit refined(entryId, m_pipeline.deliveryFallback);
        return;
    }
    m_chain->begin(chain);
    m_chain->prepare(m_plan.settings, m_pipeline.refinementSettings);
}

TranscriptRefiner *RefineAgain::acquireRefiner(const QString &providerId)
{
    releaseRefiner();
    m_refiner = m_providers.createRefinementProvider(providerId, this);
    if (!m_refiner) {
        return nullptr;
    }
    connect(m_refiner, &TranscriptRefiner::completed, this, [this](const QString &text) {
        const QString refinedText =
            TranscriptPipeline::restoreRefinedResult(m_pipeline, text).value_or(m_pipeline.deliveryFallback);
        releaseRefiner();
        emit refined(m_entryId, refinedText);
    });
    // One that fails for want of its service makes way for the next, as in
    // a dictation; any other failure ends it.
    connect(m_refiner, &TranscriptRefiner::failed, this, [this](const ProviderFailure &failure) {
        m_lastFailure = failure.message;
        if (permitsProviderFallback(failure.kind)) {
            m_chain->prepareNext(m_plan.settings, m_pipeline.refinementSettings);
            return;
        }
        fail(failure.message);
    });
    return m_refiner;
}

void RefineAgain::releaseRefiner()
{
    if (!m_refiner) {
        return;
    }
    disconnect(m_refiner, nullptr, this, nullptr);
    m_refiner->cancel();
    m_refiner->deleteLater();
    m_refiner = nullptr;
}

void RefineAgain::fail(const QString &message)
{
    releaseRefiner();
    emit failed(m_entryId, message);
}

} // namespace speecher
