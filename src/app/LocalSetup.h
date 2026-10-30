#pragma once

#include "core/LocalModelCatalog.h"
#include "core/ProviderReadiness.h"
#include "providers/CustomEndpoints.h"
#include "providers/HardwareProbe.h"
#include "providers/LocalRunner.h"

#include <QHash>
#include <QObject>
#include <QTimer>

#include <optional>

namespace speecher {

class LocalModelStore;
class ProviderAvailability;
class ProviderRegistry;
class SettingsStore;

struct RunnerChoice {
    LocalRunnerSettings selection;
    std::optional<DetectedRunner> available;
    bool offerPull = false;
    bool showSuggestion = false;
};
RunnerChoice resolveRunnerChoice(const LocalRunnerSettings &saved,
                                const QList<DetectedRunner> &runners,
                                const std::optional<CleanupModel> &suggestion);

// A local fallback this computer can run, which setup suggests adding.
// LocalSetup::setupFallbackOffer() decides whether there is one;
// LocalSetup::acceptSetupFallbackOffer() adds it.
struct SetupFallbackOffer {
    QString providerId;
    // The Local Model to download, or the cleanup model the runner serves.
    QString modelId;
    // What the suggestion names: "Parakeet 0.6B", or "Ollama".
    QString name;
};

// The machine as a model placed by runsOn sees it, for fit: the CPU budgets
// system RAM, a chosen card its own memory, and Automatic the probe's pick.
// A card that has gone away counts as the CPU, where nothing else would run.
HardwareProfile runsOnProfile(const HardwareSummary &hardware, const LocalRunsOn &runsOn);

// One choice of where Local Models run, as a localBackends kind. A saved
// backend that no card here reaches is still offered, as unavailable.
struct LocalAccelerationChoice {
    QString backend;
    bool available = true;
};
// Automatic and the CPU, then each backend that reaches a card here, in
// localBackends order.
QList<LocalAccelerationChoice> localAccelerationChoices(const QList<LocalGpu> &gpus, const LocalRunsOn &chosen);
// The cards a Runs on choice can name, once each: those of its backend, or
// for Automatic only the card an older choice is pinned to. A saved card that
// has gone away is listed last with no description. None for the CPU.
QList<LocalGpu> localGraphicsCardChoices(const QList<LocalGpu> &gpus, const LocalRunsOn &chosen);
// Where choosing an acceleration puts Local Models: a backend on its first
// card here, so fit measures the card the engine uses; Automatic and the CPU
// on none.
LocalRunsOn localRunsOnFor(const QList<LocalGpu> &gpus, const QString &backend);

// Everything a front end shows about running models on this computer beyond
// the files themselves: the hardware, Local Runners, the Custom Endpoints'
// connection tests, Speed Tests and cleanup models pulled through Ollama.
// ApplicationController owns one, so a download, a test or a pull outlives the
// window that started it. Slow work runs off the GUI thread; every change of
// state is announced by changed(), except download progress, which comes from
// LocalModelStore::downloadProgress directly.
class LocalSetup final : public QObject {
    Q_OBJECT

public:
    LocalSetup(SettingsStore &settings,
               ProviderRegistry &providers,
               LocalModelStore &models,
               QObject *parent = nullptr);

    LocalModelStore &models() const;

    // Starts the probe the first time; hardwareKnown() stays false until it
    // answers, and always in a build without local speech. Loading the speech
    // engine's backends is what makes it slow.
    void probeHardware();
    bool hardwareKnown() const;
    const HardwareSummary &hardware() const;
    const LocalModel &suggestedModel() const;
    // Against the memory of where Runs on puts models. Empty until the
    // hardware probe answers: nothing is too large before anyone knows.
    std::optional<ModelFit> fit(const LocalModel &model) const;

    // Downloads go through here so the failure a person has not seen yet can be
    // shown by whichever window opens next.
    void download(const LocalModel &model);
    void cancelDownload(const QString &modelId);
    // Deleting the model dictation uses moves it to another downloaded one.
    bool removeModel(const LocalModel &model);
    // Bytes received and total for a running download.
    std::optional<std::pair<qint64, qint64>> downloadProgress(const QString &modelId) const;

    // Runs on its own after every download that finishes. A request while
    // another test runs waits for it.
    void runSpeedTest(const QString &modelId);
    // Running or waiting its turn.
    bool speedTestRunning(const QString &modelId) const;
    std::optional<double> measuredSeconds(const QString &modelId) const;

    // Saves the hardware default once known, without marking a user choice.
    // The hardware probe settles deferred initialization for every front end.
    void initializeSpeechModel();
    void chooseSpeechModel(const QString &id);
    const LocalModel &speechModelChoice() const;
    void initializeRunner();
    RunnerChoice runnerChoice() const;

    // What the Local models page reads about a model beyond the catalog.
    struct ModelState {
        // Downloaded and the speech model dictation uses.
        bool inUse = false;
        // The model's last download or Speed Test failure, or empty.
        QString problem;
    };
    ModelState modelState(const LocalModel &model, std::optional<SpeechSettings> speech = std::nullopt) const;

    void detectRunners();
    bool detectingRunners() const;
    QList<DetectedRunner> runners() const;
    // Nothing when cleanup here would be slower than a cloud provider or none.
    std::optional<CleanupModel> suggestedCleanupModel() const;

    // Downloads a cleanup model into Ollama. Cancelling stops it with no
    // error; Ollama keeps what it has for the next pull to resume.
    void pullCleanupModel(const QString &ollamaTag);
    void cancelPull();
    struct Pull {
        QString tag;
        qint64 completedBytes = 0;
        qint64 totalBytes = 0;
        bool running = false;
        QString error;
    };
    Pull pull() const;

    // Also run on their own 800 ms after a saved endpoint in the chain gets a
    // server, or its server, path, key or format changes. A check that lists
    // models while no model is saved saves the first.
    void checkSpeechEndpoint(const SpeechEndpointSettings &endpoint);
    void checkRefinementEndpoint(const RefinementSettings &settings);

    // The local fallback setup suggests for a role: the suggested Local Model
    // once the hardware is known to fit it and it listens for the Spoken
    // Language, or a running Local Runner with a cleanup model it serves.
    std::optional<SetupFallbackOffer> setupFallbackOffer(ProviderRole role) const;
    // Whether accepting that offer behind primary would add it: not when
    // there is none, the chain behind primary is full, or the offer is
    // already primary or one of its fallbacks.
    bool setupFallbackOfferJoins(ProviderRole role, const QString &primary) const;
    // Adds that fallback after the others, behind primary, setup's choice for
    // the role, which is saved with it: the saved chain is kept in step with
    // its primary, so a fallback behind a primary not yet saved would be
    // dropped. A Local Model is chosen and downloaded the way the Local card
    // does it, a runner and its model are selected.
    void acceptSetupFallbackOffer(ProviderRole role, const QString &primary);

    // Adds reachability and the sign-ins last seen to liveFacts(), and
    // announces their changes as changed().
    void setProviderAvailability(const ProviderAvailability &availability);

    // What the app knows now about this computer and the servers in the
    // settings; see LiveFacts. draft is the settings on screen, whose
    // endpoints the verdicts must match.
    LiveFacts liveFacts() const;
    LiveFacts liveFacts(const AppSettings &draft) const;

signals:
    void changed();
    // Bytes of a running Ollama pull; pull() has them too. changed() only
    // marks its start, end and failure.
    void pullProgress(qint64 completedBytes, qint64 totalBytes);

private:
    friend class LocalSetupTestAccess;
    // The saved fallbacks behind primary with setup's offer added, or none
    // where the chain would drop it.
    std::optional<QStringList> fallbacksWithSetupOffer(ProviderRole role, const QString &primary) const;
    struct EndpointState {
        quint64 generation = 0;
        bool checking = false;
        EndpointCheck result;
    };
    void setDownloadPending(const QString &id, bool pending);
    static QString endpointStatus(const EndpointState &state);

    SettingsStore &m_settings;
    ProviderRegistry &m_providers;
    LocalModelStore &m_models;
    HardwareSummary m_hardware;
    bool m_hardwareProbing = false;
    bool m_hardwareKnown = false;
    QHash<QString, std::pair<qint64, qint64>> m_progress;
    QHash<QString, QString> m_downloadErrors;
    QString m_speedTestModel;
    // Where m_speedTestModel's test runs, which is what its result is kept
    // under even if Runs on changes meanwhile.
    LocalRunsOn m_speedTestRunsOn;
    QStringList m_speedTestQueue;
    QHash<QString, QString> m_speedTestErrors;
    // Where the loaded Local Model runs; see LiveFacts::localModelRunsOn.
    QString m_modelRunsOn;
    QList<DetectedRunner> m_runners;
    bool m_detectingRunners = false;
    bool m_runnersChecked = false;
    const ProviderAvailability *m_availability = nullptr;
    OllamaPull m_ollamaPull;
    Pull m_pull;
    std::optional<SpeechEndpointSettings> m_checkedSpeech;
    std::optional<RefinementEndpoint> m_checkedRefinement;
    EndpointState m_speechEndpoint;
    EndpointState m_refinementEndpoint;
    QTimer m_speechCheckDelay;
    QTimer m_refinementCheckDelay;
};

} // namespace speecher
