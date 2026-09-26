#include "app/LocalSetup.h"

#include "core/SettingsStore.h"
#include "providers/LocalModelStore.h"
#include "providers/ProviderProbe.h"
#include "providers/ProviderRegistry.h"
#ifdef SPEECHER_WITH_LOCAL_SPEECH
#include "providers/LocalSpeechTranscriber.h"
#endif

#include <QDir>
#include <QTimer>

namespace speecher {
namespace {

// Decimal gigabytes, as memory is sold and download sizes are quoted.
QString gigabytesText(quint64 bytes)
{
    return QStringLiteral("%1 GB").arg(qRound(double(bytes) / 1e9));
}

QString acceleratorName(const QString &kind)
{
    if (kind == QStringLiteral("metal")) return QStringLiteral("Metal");
    if (kind == QStringLiteral("vulkan")) return QStringLiteral("Vulkan");
    if (kind == QStringLiteral("cuda")) return QStringLiteral("CUDA");
    return kind;
}

} // namespace

LocalSetup::LocalSetup(SettingsStore &settings,
                       ProviderRegistry &providers,
                       LocalModelStore &models,
                       QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_providers(providers)
    , m_models(models)
{
    connect(&m_models, &LocalModelStore::downloadProgress, this,
            [this](const QString &modelId, qint64 received, qint64 total) {
                m_progress.insert(modelId, {received, total});
            });
    connect(&m_models, &LocalModelStore::downloadFinished, this, [this](const QString &modelId) {
        m_progress.remove(modelId);
        emit changed();
        runSpeedTest(modelId);
    });
    connect(&m_models, &LocalModelStore::downloadFailed, this,
            [this](const QString &modelId, const QString &message) {
                m_progress.remove(modelId);
                m_downloadErrors.insert(modelId, message);
                emit changed();
            });
    connect(&m_ollamaPull, &OllamaPull::progress, this,
            [this](qint64 completed, qint64 total, const QString &) {
                m_pull.completedBytes = completed;
                m_pull.totalBytes = total;
                emit pullProgress(completed, total);
            });
    connect(&m_ollamaPull, &OllamaPull::finished, this, [this] {
        m_pull.running = false;
        LocalRunnerSettings runner = m_settings.localRunnerSettings();
        runner.runner = QStringLiteral("ollama");
        runner.model = m_pull.tag;
        m_settings.setLocalRunnerSettings(runner);
        emit cleanupModelPulled(m_pull.tag);
        detectRunners();
    });
    connect(&m_ollamaPull, &OllamaPull::failed, this, [this](const QString &message) {
        m_pull.running = false;
        m_pull.error = message;
        emit changed();
    });
#ifdef SPEECHER_WITH_LOCAL_SPEECH
    if (auto *local = qobject_cast<LocalSpeechTranscriber *>(
            m_providers.speechProvider(QStringLiteral("local")))) {
        connect(local, &LocalSpeechTranscriber::speedTestFinished, this,
                [this](const QString &modelId, double seconds, const QString &error) {
                    m_speedTestModel.clear();
                    if (!m_speedTestQueue.isEmpty()) {
                        QTimer::singleShot(0, this, [this, next = m_speedTestQueue.takeFirst()] {
                            runSpeedTest(next);
                        });
                    }
                    if (error.isEmpty()) {
                        m_speedTestErrors.remove(modelId);
                        LocalSpeechSettings local = m_settings.localSpeechSettings();
                        local.speedTestSeconds.insert(modelId, seconds);
                        m_settings.setLocalSpeechSettings(local);
                    } else {
                        m_speedTestErrors.insert(modelId, error);
                    }
                    emit changed();
                });
    }
#endif
}

LocalModelStore &LocalSetup::models() const
{
    return m_models;
}

void LocalSetup::probeHardware()
{
    if (m_hardwareKnown || m_hardwareProbing) {
        return;
    }
    m_hardwareProbing = true;
#ifdef SPEECHER_WITH_LOCAL_SPEECH
    runProviderProbe<HardwareSummary>(&m_providers, this, [] { return speecher::probeHardware(); },
                                      [this](const HardwareSummary &summary) {
                                          m_hardware = summary;
                                          m_hardwareProbing = false;
                                          m_hardwareKnown = true;
                                          emit changed();
                                      });
#else
    // Without the speech engine there is nothing to ask; the hardware stays
    // unknown rather than read as zero memory.
    m_hardwareProbing = false;
#endif
}

bool LocalSetup::hardwareKnown() const
{
    return m_hardwareKnown;
}

const HardwareSummary &LocalSetup::hardware() const
{
    return m_hardware;
}

QString LocalSetup::hardwareLine() const
{
    if (!m_hardwareKnown) {
        return QStringLiteral("Checking this computer…");
    }
    const HardwareProfile &profile = m_hardware.profile;
    QStringList parts;
    parts << QStringLiteral("%1, %2 threads").arg(profile.chipName).arg(m_hardware.cpuThreads);
    if (m_hardware.gpus.isEmpty()) {
        parts << QStringLiteral("no graphics acceleration");
    } else {
        const LocalSpeechEngine::Device &gpu = m_hardware.gpus.first();
        parts << QStringLiteral("%1, %2").arg(gpu.description, acceleratorName(gpu.kind));
    }
    parts << (profile.accelerator == HardwareProfile::Accelerator::DedicatedGpu
                  ? QStringLiteral("%1 memory, %2 on the graphics card")
                        .arg(gigabytesText(profile.systemRamBytes), gigabytesText(profile.gpuMemoryBytes))
                  : QStringLiteral("%1 memory").arg(gigabytesText(profile.systemRamBytes)));
    return parts.join(QStringLiteral(" · "));
}

const LocalModel &LocalSetup::suggestedModel() const
{
    return suggestedLocalModel(m_hardware.profile);
}

std::optional<ModelFit> LocalSetup::fit(const LocalModel &model) const
{
    return m_hardwareKnown ? std::optional<ModelFit>(modelFit(model, m_hardware.profile)) : std::nullopt;
}

QString LocalSetup::fitLabel(const LocalModel &model) const
{
    const std::optional<ModelFit> known = fit(model);
    return known ? modelFitLabel(*known) : QStringLiteral("Checking…");
}

void LocalSetup::download(const LocalModel &model)
{
    m_downloadErrors.remove(model.id);
    m_progress.insert(model.id, {0, model.sizeBytes});
    m_models.download(model);
    emit changed();
}

void LocalSetup::cancelDownload(const QString &modelId)
{
    m_models.cancel(modelId);
    m_progress.remove(modelId);
    emit changed();
}

bool LocalSetup::removeModel(const LocalModel &model)
{
    const bool removed = m_models.remove(model);
    m_progress.remove(model.id);
    m_speedTestQueue.removeAll(model.id);
    LocalSpeechSettings local = m_settings.localSpeechSettings();
    local.speedTestSeconds.remove(model.id);
    // Dictation moves to another downloaded model rather than a missing file.
    // With none left it stays, and dictating says to download one.
    if (local.modelId == model.id) {
        for (const LocalModel &other : localModelCatalog()) {
            if (m_models.isDownloaded(other)) {
                local.modelId = other.id;
                break;
            }
        }
    }
    m_settings.setLocalSpeechSettings(local);
    emit changed();
    return removed;
}

std::optional<std::pair<qint64, qint64>> LocalSetup::downloadProgress(const QString &modelId) const
{
    if (!m_models.isDownloading(modelId)) {
        return std::nullopt;
    }
    return m_progress.value(modelId);
}

QString LocalSetup::downloadError(const QString &modelId) const
{
    return m_downloadErrors.value(modelId);
}

void LocalSetup::runSpeedTest(const QString &modelId)
{
#ifdef SPEECHER_WITH_LOCAL_SPEECH
    auto *local = qobject_cast<LocalSpeechTranscriber *>(m_providers.speechProvider(QStringLiteral("local")));
    if (!local || m_speedTestModel == modelId || m_speedTestQueue.contains(modelId)) {
        return;
    }
    // One test at a time, on the engine dictation uses; the rest wait their
    // turn. Every path of LocalSpeechTranscriber::runSpeedTest answers with
    // speedTestFinished, which starts the next.
    if (!m_speedTestModel.isEmpty()) {
        m_speedTestQueue.append(modelId);
        return;
    }
    m_speedTestModel = modelId;
    m_speedTestErrors.remove(modelId);
    emit changed();
    local->runSpeedTest(modelId, m_settings.localSpeechSettings().deviceId);
#else
    Q_UNUSED(modelId);
#endif
}

bool LocalSetup::speedTestRunning(const QString &modelId) const
{
    return m_speedTestModel == modelId || m_speedTestQueue.contains(modelId);
}

std::optional<double> LocalSetup::measuredSeconds(const QString &modelId) const
{
    const QMap<QString, double> measured = m_settings.localSpeechSettings().speedTestSeconds;
    const auto it = measured.constFind(modelId);
    return it == measured.cend() ? std::nullopt : std::optional<double>(*it);
}

QString LocalSetup::speedTestError(const QString &modelId) const
{
    return m_speedTestErrors.value(modelId);
}

void LocalSetup::detectRunners()
{
    if (m_detectingRunners) {
        return;
    }
    m_detectingRunners = true;
    emit changed();
    runProviderProbe<QList<DetectedRunner>>(&m_providers, this, [] { return detectLocalRunners(); },
                                            [this](const QList<DetectedRunner> &runners) {
                                                m_runners = runners;
                                                m_detectingRunners = false;
                                                emit changed();
                                            });
}

bool LocalSetup::detectingRunners() const
{
    return m_detectingRunners;
}

QList<DetectedRunner> LocalSetup::runners() const
{
    return m_runners;
}

std::optional<CleanupModel> LocalSetup::suggestedCleanupModel() const
{
    return speecher::suggestedCleanupModel(cleanupHardwareFor(m_hardware.profile));
}

void LocalSetup::pullCleanupModel(const QString &ollamaTag)
{
    m_pull = {ollamaTag, 0, 0, true, {}};
    m_ollamaPull.start(ollamaTag);
    emit changed();
}

LocalSetup::Pull LocalSetup::pull() const
{
    return m_pull;
}

void LocalSetup::checkSpeechEndpoint(const SpeechEndpointSettings &endpoint)
{
    m_speechEndpoint.checking = true;
    emit changed();
    runProviderProbe<EndpointCheck>(&m_providers, this,
                                    [endpoint] { return speecher::checkSpeechEndpoint(endpoint); },
                                    [this](const EndpointCheck &check) {
                                        m_speechEndpoint = {false, check};
                                        emit changed();
                                    });
}

void LocalSetup::checkRefinementEndpoint(const RefinementSettings &settings)
{
    m_refinementEndpoint.checking = true;
    emit changed();
    runProviderProbe<EndpointCheck>(&m_providers, this,
                                    [settings] { return speecher::checkRefinementEndpoint(settings); },
                                    [this](const EndpointCheck &check) {
                                        m_refinementEndpoint = {false, check};
                                        emit changed();
                                    });
}

QString LocalSetup::endpointStatus(const EndpointState &state)
{
    return state.checking ? QStringLiteral("Checking…") : state.result.message;
}

LiveFacts LocalSetup::liveFacts() const
{
    LiveFacts facts;
    facts.speechEndpointStatus = endpointStatus(m_speechEndpoint);
    facts.speechEndpointModels = m_speechEndpoint.result.models;
    facts.refinementEndpointStatus = endpointStatus(m_refinementEndpoint);
    facts.refinementEndpointModels = m_refinementEndpoint.result.models;
    for (const DetectedRunner &runner : m_runners) {
        facts.runners.append({runner.id,
                              runner.version.isEmpty()
                                  ? runner.name
                                  : QStringLiteral("%1 %2").arg(runner.name, runner.version)});
        facts.runnerModels.insert(runner.id, runner.models);
    }
    facts.detectingRunners = m_detectingRunners;
    qint64 used = 0;
    for (const LocalModel &model : localModelCatalog()) {
        if (m_models.isDownloaded(model)) {
            used += model.sizeBytes;
        }
    }
    facts.modelFolder = QStringLiteral("%1 · %2 used")
                            .arg(QDir::toNativeSeparators(m_models.directory()), downloadSizeText(used));
    if (m_hardware.gpus.size() > 1) {
        for (const LocalSpeechEngine::Device &gpu : m_hardware.gpus) {
            facts.gpus.append({gpu.id, gpu.description});
        }
    }
    return facts;
}

} // namespace speecher
