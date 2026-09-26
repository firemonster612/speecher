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
    // answers. Loading the speech engine's backends is what makes it slow.
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
    bool removeModel(const LocalModel &model);
    // Bytes received and total for a running download.
    std::optional<std::pair<qint64, qint64>> downloadProgress(const QString &modelId) const;
    QString downloadError(const QString &modelId) const;

    // Runs on its own after every download that finishes.
    void runSpeedTest(const QString &modelId);
    bool speedTestRunning(const QString &modelId) const;
    std::optional<double> measuredSeconds(const QString &modelId) const;
    QString speedTestError(const QString &modelId) const;

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

    // What the schema's rows report; see LiveFacts.
    LiveFacts liveFacts() const;

signals:
    void changed();
    void cleanupModelPulled(const QString &ollamaTag);

private:
    struct EndpointState {
        bool checking = false;
        EndpointCheck result;
    };
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
    QHash<QString, QString> m_speedTestErrors;
    QList<DetectedRunner> m_runners;
    bool m_detectingRunners = false;
    OllamaPull m_ollamaPull;
    Pull m_pull;
    EndpointState m_speechEndpoint;
    EndpointState m_refinementEndpoint;
};

} // namespace speecher
