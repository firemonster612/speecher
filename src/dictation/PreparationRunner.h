#pragma once

#include "dictation/DictationPorts.h"

#include <QList>
#include <QObject>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

class QThread;

namespace speecher {

// Runs provider preparations off the GUI thread, one at a time: each start
// cancels the one before, so only the latest reports, and only its jobs
// apply.
class PreparationRunner : public QObject {
    Q_OBJECT

public:
    explicit PreparationRunner(QObject *parent = nullptr);
    ~PreparationRunner() override;

    // Prepares a speech provider, then renews the refiner's sign-in if the
    // speech provider prepared. Without a speech job the speech provider
    // counts as prepared. Ends in speechPrepared().
    void prepareSpeech(std::optional<PreparationJob> speechJob, std::optional<PreparationJob> refinerJob);
    // Renews a refiner's sign-in. Ends in refinerPrepared().
    void prepareRefiner(PreparationJob refinerJob);
    // Reads the keys providerId needs in role and settings has not read
    // (SettingsStore::resolveProviderSecrets). Ends in secretsResolved().
    void resolveSecrets(ProviderRole role, const QString &providerId, AppSettings settings);
    void cancel();

signals:
    void speechPrepared(const std::optional<speecher::ProviderFailure> &failure);
    void refinerPrepared(const std::optional<speecher::ProviderFailure> &failure);
    // settings with the keys read, unless a key could not be.
    void secretsResolved(const std::optional<speecher::ProviderFailure> &failure,
                         const speecher::AppSettings &settings);

private:
    struct Preparation;
    void run(std::function<void(const std::atomic_bool &cancelled)> work, std::function<void()> finish);
    std::shared_ptr<Preparation> m_current;
    QList<QThread *> m_threads;
};

} // namespace speecher
