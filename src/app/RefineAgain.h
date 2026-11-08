#pragma once

// Refine again: a History Entry's Raw Transcript refined once more with a
// Writing Profile, the way a dictation into no app refines it.

#include "dictation/DictationPlan.h"
#include "dictation/TranscriptPipeline.h"

#include <QObject>
#include <QPointer>

namespace speecher {

class ProviderRegistry;
class RefinementChain;
class SettingsStore;
class TranscriptRefiner;

// One refinement at a time: the plan is planDictation's with the chosen
// profile and an empty Target, so refinement sees no nearby text and no
// screenshot, and the refinement Service Order is walked with its fallbacks
// as a dictation walks it. A new refine() drops the one under way.
class RefineAgain : public QObject {
    Q_OBJECT

public:
    RefineAgain(SettingsStore &settings, ProviderRegistry &providers, QObject *parent = nullptr);

    // Ends in refined or failed for entryId, at once or later. Clean up my
    // words off doesn't stop it, as the person asked; no refinement service,
    // or a Cleanup Level of None, fails it with why.
    void refine(qint64 entryId, const QString &raw, const QString &writingProfile);

signals:
    void refined(qint64 entryId, const QString &text);
    // message: why no refiner refined it, in the refiner's words.
    void failed(qint64 entryId, const QString &message);

private:
    TranscriptRefiner *acquireRefiner(const QString &providerId);
    void releaseRefiner();
    void fail(const QString &message);

    SettingsStore &m_settings;
    ProviderRegistry &m_providers;
    RefinementChain *m_chain;
    QPointer<TranscriptRefiner> m_refiner;
    qint64 m_entryId = 0;
    DictationPlan m_plan;
    TranscriptPipelineResult m_pipeline;
    // Why the last refiner tried could not refine.
    QString m_lastFailure;
};

} // namespace speecher
