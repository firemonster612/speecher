#pragma once

#include "core/LocalModelCatalog.h"
#include "core/settings/SettingsSchema.h"
#include "providers/CustomEndpoints.h"
#include "providers/HardwareProbe.h"
#include "providers/LocalRunner.h"

#include <QHash>
#include <QObject>

#include <optional>

namespace speecher {

class LocalModelStore;
class ProviderRegistry;
class SettingsStore;

// Readiness never overrides a saved own-model choice.
// Speech endpoints are configured in Settings but remain visible when saved.
bool offersSetupSpeechProvider(const QString &id, const QString &saved, bool localAvailable);
bool isSetupSignInProvider(const QString &id);
QString setupProviderChoice(const QString &saved, const QStringList &ready, bool explicitlyChosen);
// Refinement is optional: an unready default sign-in with no ready one to
// move to gives way to a runner on this computer, or to "none". A saved
// provider is kept. Asked once the provider checks and the runner check have
// answered.
QString setupRefinementChoice(const QString &saved, const QStringList &ready, bool runnerFound,
                              bool explicitlyChosen);

// One per assistant, retained across Back/Next. update returns the speech
// provider to persist; a missing userChoice follows completed sign-in checks.
class WelcomeChoice {
public:
    QString update(const QString &provider, const QStringList &readyProviders,
                   bool proxyAccountFound, std::optional<bool> userChoice = std::nullopt);
    // An explicit Transcription choice is no longer an automatic path write.
    void providerChosen() { m_previousProvider.reset(); }
    bool local() const { return m_local; }
    bool ready() const { return m_local || m_signInFound; }
private:
    std::optional<bool> m_explicit;
    std::optional<QString> m_previousProvider;
    bool m_local = false;
    bool m_signInFound = false;
};

struct RunnerChoice {
    LocalRunnerSettings selection;
    std::optional<DetectedRunner> available;
    bool offerPull = false;
    bool showSuggestion = false;
};
RunnerChoice resolveRunnerChoice(const LocalRunnerSettings &saved,
                                const QList<DetectedRunner> &runners,
                                const std::optional<CleanupModel> &suggestion);

// What the Ready step names for an own-model refinement choice: "Ollama with
// gemma4:e4b", "qwen3 on your server". Empty for any other provider.
QString ownModelRefinementSummary(const RefinementSettings &settings);

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
    // "AMD Ryzen 7 PRO 4750U, 16 threads · AMD Radeon Graphics, Vulkan · 16 GB".
    QString hardwareLine() const;
    const LocalModel &suggestedModel() const;
    // Empty until the hardware probe answers: nothing is too large before
    // anyone knows.
    std::optional<ModelFit> fit(const LocalModel &model) const;
    // What a list shows for fit: "Fits", "Tight fit", "Too large", or
    // "Checking…" before the probe answers.
    QString fitLabel(const LocalModel &model) const;

    // Downloads go through here so the failure a person has not seen yet can be
    // shown by whichever window opens next.
    void download(const LocalModel &model);
    void cancelDownload(const QString &modelId);
    // Deleting the model dictation uses moves it to another downloaded one.
    bool removeModel(const LocalModel &model);
    // Bytes received and total for a running download.
    std::optional<std::pair<qint64, qint64>> downloadProgress(const QString &modelId) const;
    QString downloadError(const QString &modelId) const;

    // Runs on its own after every download that finishes. A request while
    // another test runs waits for it.
    void runSpeedTest(const QString &modelId);
    // Running or waiting its turn.
    bool speedTestRunning(const QString &modelId) const;
    std::optional<double> measuredSeconds(const QString &modelId) const;
    QString speedTestError(const QString &modelId) const;

    // Saves the hardware default once known, without marking a user choice.
    // The hardware probe settles deferred initialization for every front end.
    void initializeSpeechModel();
    void chooseSpeechModel(const QString &id);
    const LocalModel &speechModelChoice() const;
    void initializeRunner();
    RunnerChoice runnerChoice() const;

    // What every front end shows for a model on the Local card and in the
    // comparison table.
    struct ModelState {
        bool downloaded = false;
        bool downloading = false;
        bool inUse = false;
        // Only once the hardware is known.
        bool suggested = false;
        bool tooLarge = false;
        QString problem;
        QString speedText;
        QString speedDetail;
        // The card's three lines: when text shows, speed, word errors.
        QString cardFacts;
        // Model, Download, Word errors, 10 s of speech, Text shows, Memory.
        QStringList tableCells;
    };
    ModelState modelState(const LocalModel &model, std::optional<SpeechSettings> speech = std::nullopt) const;

    void detectRunners();
    bool detectingRunners() const;
    QList<DetectedRunner> runners() const;
    // Nothing when cleanup here would be slower than a cloud provider or none.
    std::optional<CleanupModel> suggestedCleanupModel() const;

    // Downloads a cleanup model into Ollama.
    void pullCleanupModel(const QString &ollamaTag);
    struct Pull {
        QString tag;
        qint64 completedBytes = 0;
        qint64 totalBytes = 0;
        bool running = false;
        QString error;
    };
    Pull pull() const;

    void checkSpeechEndpoint(const SpeechEndpointSettings &endpoint);
    void checkRefinementEndpoint(const RefinementSettings &settings);

    // The settings rows' actions this class answers: the endpoint tests (of
    // the settings shown), runner detection and the model folder. False for
    // any other row.
    bool runSettingsAction(const QString &rowId, const AppSettings &shown);

    // What the schema's rows report; see LiveFacts.
    LiveFacts liveFacts() const;
    LiveFacts liveFacts(const AppSettings &draft) const;

signals:
    void changed();
    // Bytes of a running Ollama pull; pull() has them too. changed() only
    // marks its start, end and failure.
    void pullProgress(qint64 completedBytes, qint64 totalBytes);
    void cleanupModelPulled(const QString &ollamaTag);

private:
    friend class LocalSetupTestAccess;
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
    QStringList m_speedTestQueue;
    QHash<QString, QString> m_speedTestErrors;
    QList<DetectedRunner> m_runners;
    bool m_detectingRunners = false;
    OllamaPull m_ollamaPull;
    Pull m_pull;
    std::optional<SpeechEndpointSettings> m_checkedSpeech;
    std::optional<RefinementEndpoint> m_checkedRefinement;
    EndpointState m_speechEndpoint;
    EndpointState m_refinementEndpoint;
};

} // namespace speecher
