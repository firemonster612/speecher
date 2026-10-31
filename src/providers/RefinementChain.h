#pragma once

#include "core/ProviderAttemptIssue.h"
#include "core/ProviderReadiness.h"
#include "providers/TranscriptRefiner.h"

#include <QPointer>
#include <QStringList>

#include <functional>

namespace speecher {

class PreparationRunner;

// A session's refiners, primary first, and the one it is on. It walks forward
// only: preparing finds the first refiner from the current one on that
// prepares, passing over one that needs the internet while the system says it
// is offline, is unknown, or fails to prepare. Its keys come from the keyring,
// and an expired sign-in renews, off this thread; the refiner's prepare() on
// this one never renews.
class RefinementChain : public QObject {
    Q_OBJECT

public:
    // The session's refiner for providerId, connected to it; null for an id
    // the registry does not offer.
    using Acquire = std::function<TranscriptRefiner *(const QString &providerId)>;

    explicit RefinementChain(Acquire acquire, QObject *parent = nullptr);

    // Starts over at primary, with fallbacks after it; none for
    // kNoRefinementProvider. Cancels a preparation still running.
    void begin(const QString &primary, const QStringList &fallbacks);
    const QStringList &providerIds() const;
    // The refiner the session is on.
    QString currentId() const;
    // refinement as the current refiner runs it: its id, with the keys the
    // walk has read.
    RefinementSettings currentSettings() const;

    // What the system says about the internet. Only refiners that need it are
    // passed over while offline.
    void setReachability(Reachability reachability);

    // Prepares the current refiner, or the first after it that prepares, to
    // refine with refinement under settings: ends in ready() or exhausted(),
    // at once or later.
    void prepare(const AppSettings &settings, const RefinementSettings &refinement);
    // Passes the current refiner over and prepares from the next.
    void prepareNext(const AppSettings &settings, const RefinementSettings &refinement);
    // Nothing running reports back.
    void cancel();

signals:
    // A refiner the walk passed over, and why.
    void passedOver(const QString &providerId, speecher::ProviderAttemptIssue::Stage stage,
                    const speecher::ProviderFailure &failure);
    // The walk waits for keys or a sign-in off this thread.
    void preparing();
    // The keys the walk read, in the settings it was given. The session
    // keeps them; the walk goes on with them.
    void secretsResolved(const speecher::AppSettings &settings);
    // currentId() prepared and may refine.
    void ready();
    // No refiner is left to refine.
    void exhausted();

private:
    void walk();
    void prepareCurrent();
    void passOver(const ProviderFailure &failure);
    void finishPreparation(const std::optional<ProviderFailure> &failure);
    void useResolvedSecrets(const std::optional<ProviderFailure> &failure, const AppSettings &settings);

    Acquire m_acquire;
    PreparationRunner *m_runner;
    QStringList m_providerIds;
    int m_index = 0;
    Reachability m_reachability = Reachability::Unknown;
    // What the walk under way prepares with.
    AppSettings m_settings;
    RefinementSettings m_refinement;
    QPointer<TranscriptRefiner> m_refiner;
    // Moves on as a walk is cancelled or another starts. A handler of any
    // signal may do either, so the walk checks it after each one it emits and
    // goes no further once it moved.
    quint64 m_walkGeneration = 0;
};

} // namespace speecher
