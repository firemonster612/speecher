#pragma once

#include "dictation/DictationPorts.h"

#include <QList>
#include <QObject>

#include <memory>
#include <optional>

class QThread;

namespace speecher {

struct StartupPreparationResult {
    // The caller's own number for this preparation, so it can tell which of
    // several it is hearing about.
    quint64 revision = 0;
    // The role of the provider being prepared. A key that cannot be read
    // fails speech for Speech and refinerRefresh for Refinement.
    ProviderRole role = ProviderRole::Speech;
    SpeechPrepareResult speech;
    std::optional<AppSettings> resolvedSettings;
    RefinementRefreshResult refinerRefresh;
    bool refinerRefreshAttempted = false;
};

class StartupPreparationRunner : public QObject {
    Q_OBJECT

public:
    explicit StartupPreparationRunner(QObject *parent = nullptr);
    ~StartupPreparationRunner() override;

    // Each start or resolveSecrets cancels the one before: only the latest
    // completes, and only its jobs apply.
    void start(quint64 revision,
               std::optional<SpeechPrepareJob> speechJob,
               std::optional<RefinementRefreshJob> refinerJob,
               SpeechPrepareResult speechPrepared,
               ProviderRole role = ProviderRole::Speech);
    // Reads the keys providerId needs in role and settings has not read
    // (SettingsStore::resolveProviderSecrets) on the worker; completes with
    // them in resolvedSettings.
    void resolveSecrets(quint64 revision, ProviderRole role, const QString &providerId, AppSettings settings);
    void cancel();

signals:
    void completed(const speecher::StartupPreparationResult &result);

private:
    struct Preparation;
    void run(std::shared_ptr<Preparation> preparation);
    std::shared_ptr<Preparation> m_current;
    QList<QThread *> m_threads;
};

} // namespace speecher

Q_DECLARE_METATYPE(speecher::StartupPreparationResult)
