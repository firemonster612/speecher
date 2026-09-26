#include "app/LocalSetup.h"

#include "core/SettingsStore.h"
#include "core/settings/SettingsKeys.h"
#include "providers/LocalModelStore.h"
#include "providers/ProviderProbe.h"
#include "providers/ProviderRegistry.h"
#ifdef SPEECHER_WITH_LOCAL_SPEECH
#include "providers/LocalSpeechTranscriber.h"
#endif

#include <QDesktopServices>
#include <QDir>
#include <QTimer>
#include <QUrl>

#include <algorithm>

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

bool offersSetupSpeechProvider(const QString &id, const QString &saved, bool localAvailable)
{
    return (id != QStringLiteral("endpoint") || id == saved)
        && (id != QStringLiteral("local") || localAvailable);
}

bool isSetupSignInProvider(const QString &id)
{
    return id == QStringLiteral("claude") || id == QStringLiteral("codex")
        || id == QStringLiteral("openai") || id == QStringLiteral("anthropic");
}

QString setupProviderChoice(const QString &saved, const QStringList &ready, bool explicitlyChosen)
{
    if (explicitlyChosen || !isSetupSignInProvider(saved) || ready.contains(saved)) return saved;
    for (const auto &id : ready) {
        if (isSetupSignInProvider(id)) return id;
    }
    return saved;
}

QString WelcomeChoice::update(const QString &provider, const QStringList &readyProviders,
                              bool proxyAccountFound, std::optional<bool> userChoice)
{
    QStringList signIns;
    for (const auto &id : readyProviders) {
        if (isSetupSignInProvider(id)) signIns.append(id);
    }
    m_signInFound = proxyAccountFound || !signIns.isEmpty();
    if (userChoice) m_explicit = userChoice;
    const bool local = m_explicit.value_or(!m_signInFound);
    const bool transition = local != m_local;
    m_local = local;
    if (!transition && !userChoice) return provider;
    if (local) {
        if (provider != QStringLiteral("local") && (userChoice || isSetupSignInProvider(provider))) {
            m_previousProvider = provider;
            return QStringLiteral("local");
        }
        return provider;
    }
    if (m_previousProvider && provider == QStringLiteral("local")) {
        const QString previous = *m_previousProvider;
        m_previousProvider.reset();
        return userChoice && !isSetupSignInProvider(previous) && !signIns.isEmpty()
            ? signIns.first() : setupProviderChoice(previous, signIns, false);
    }
    m_previousProvider.reset();
    return userChoice && !signIns.isEmpty() ? signIns.first() : provider;
}

RunnerChoice resolveRunnerChoice(const LocalRunnerSettings &saved,
                                const QList<DetectedRunner> &runners,
                                const std::optional<CleanupModel> &suggestion)
{
    RunnerChoice choice{saved, {}, false};
    const auto found = std::find_if(runners.cbegin(), runners.cend(), [&saved](const auto &runner) {
        return runner.id == saved.runner;
    });
    if (found != runners.cend()) choice.available = *found;
    else if (saved.runner.isEmpty() && saved.model.isEmpty() && !runners.isEmpty()) {
        choice.available = runners.first();
        choice.selection.runner = choice.available->id;
        const auto &models = choice.available->models;
        const auto preferred = std::find_if(models.cbegin(), models.cend(), [&suggestion](const auto &model) {
            return suggestion && model.startsWith(suggestion->ollamaTag);
        });
        if (!models.isEmpty()) choice.selection.model = preferred == models.cend() ? models.first() : *preferred;
    }
    if (choice.available && choice.available->id == QStringLiteral("ollama") && suggestion) {
        const auto &models = choice.available->models;
        choice.offerPull = !models.contains(suggestion->ollamaTag)
            && !models.contains(suggestion->ollamaTag + QStringLiteral(":latest"));
    }
    choice.showSuggestion = choice.available && choice.available->id == QStringLiteral("ollama")
        && (!suggestion || choice.offerPull);
    return choice;
}

QString ownModelRefinementSummary(const RefinementSettings &settings)
{
    if (settings.providerId == QStringLiteral("local")) {
        const QString runner = localRunnerName(settings.localRunner.runner);
        return settings.localRunner.model.isEmpty()
            ? runner : QStringLiteral("%1 with %2").arg(runner, settings.localRunner.model);
    }
    if (settings.providerId == QStringLiteral("endpoint")) {
        return settings.endpoint.model.isEmpty()
            ? QStringLiteral("your server") : QStringLiteral("%1 on your server").arg(settings.endpoint.model);
    }
    return {};
}

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
        setDownloadPending(modelId, false);
        emit changed();
        runSpeedTest(modelId);
    });
    connect(&m_models, &LocalModelStore::downloadFailed, this,
            [this](const QString &modelId, const QString &message) {
                m_progress.remove(modelId);
                m_downloadErrors.insert(modelId, message);
                setDownloadPending(modelId, false);
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
        runner.model = ollamaListedName(m_pull.tag);
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
    QTimer::singleShot(0, this, [this] {
        const auto pending = m_settings.raw().value(SettingsKeys::LocalPendingDownloads).toStringList();
        for (const auto &id : pending) {
            const auto *model = findLocalModel(id);
            if (model && !m_models.isDownloaded(*model)) download(*model);
            else setDownloadPending(id, false);
        }
    });
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
                                          initializeSpeechModel();
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

void LocalSetup::setDownloadPending(const QString &id, bool pending)
{
    auto ids = m_settings.raw().value(SettingsKeys::LocalPendingDownloads).toStringList();
    ids.removeAll(id);
    if (pending) ids.append(id);
    m_settings.raw().setValue(SettingsKeys::LocalPendingDownloads, ids);
    m_settings.raw().sync();
}

void LocalSetup::download(const LocalModel &model)
{
    if (m_models.isDownloaded(model) || m_models.isDownloading(model.id)) return;
    setDownloadPending(model.id, true);
    m_downloadErrors.remove(model.id);
    m_progress.insert(model.id, {0, model.sizeBytes});
    m_models.download(model);
    emit changed();
}

void LocalSetup::cancelDownload(const QString &modelId)
{
    setDownloadPending(modelId, false);
    m_models.cancel(modelId);
    m_progress.remove(modelId);
    emit changed();
}

bool LocalSetup::removeModel(const LocalModel &model)
{
    setDownloadPending(model.id, false);
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

const LocalModel &LocalSetup::speechModelChoice() const
{
    const auto saved = m_settings.localSpeechSettings();
    const auto *model = findLocalModel(saved.modelId);
    return saved.modelChosen && model ? *model : suggestedModel();
}

void LocalSetup::initializeSpeechModel()
{
    if (!m_hardwareKnown || m_settings.localSpeechSettings().modelChosen) return;
    // The general settings setter treats a changed model as a user choice.
    // A hardware default must remain replaceable until the person chooses.
    m_settings.raw().setValue(SettingsKeys::LocalModel, suggestedModel().id);
    m_settings.raw().setValue(SettingsKeys::LocalModelChosen, false);
}

void LocalSetup::chooseSpeechModel(const QString &id)
{
    if (!findLocalModel(id)) return;
    auto local = m_settings.localSpeechSettings();
    local.modelId = id;
    local.modelChosen = true;
    m_settings.setLocalSpeechSettings(local);
    emit changed();
}

RunnerChoice LocalSetup::runnerChoice() const
{
    return resolveRunnerChoice(m_settings.localRunnerSettings(), m_runners, suggestedCleanupModel());
}

void LocalSetup::initializeRunner()
{
    const auto saved = m_settings.localRunnerSettings();
    if (!saved.runner.isEmpty() || !saved.model.isEmpty()) return;
    const auto choice = runnerChoice();
    if (choice.available) m_settings.setLocalRunnerSettings(choice.selection);
}

LocalSetup::ModelState LocalSetup::modelState(const LocalModel &model, std::optional<SpeechSettings> draft) const
{
    SpeechSettings speech;
    speech.providerId = m_settings.speechProvider();
    speech.local = m_settings.localSpeechSettings();
    if (draft) speech = *draft;
    ModelState state;
    state.downloading = m_models.isDownloading(model.id);
    state.downloaded = !state.downloading && m_models.isDownloaded(model);
    state.inUse = state.downloaded && speech.providerId == QStringLiteral("local") && speech.local.modelId == model.id;
    state.problem = m_downloadErrors.value(model.id, m_speedTestErrors.value(model.id));
    const auto measured = measuredSeconds(model.id);
    const auto estimate = m_hardwareKnown ? estimatedSpeed(model, m_hardware.profile) : std::nullopt;
    state.speedText = measured ? speechSecondsText(*measured)
        : estimate ? QStringLiteral("~") + speechSecondsText(estimate->secondsFor10sSpeech) + QStringLiteral(" (estimated)")
                   : QStringLiteral("Not measured");
    state.speedDetail = speedTestRunning(model.id) ? QStringLiteral("Testing…")
        : measured || estimate ? localModelSpeedLine(model, m_hardware.profile, measured)
                               : QStringLiteral("Not measured");
    return state;
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
                                                initializeRunner();
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
    const auto generation = ++m_speechEndpoint.generation;
    m_checkedSpeech = endpoint;
    m_speechEndpoint.result = {};
    m_speechEndpoint.checking = true;
    emit changed();
    runProviderProbe<EndpointCheck>(&m_providers, this,
                                    [endpoint] { return speecher::checkSpeechEndpoint(endpoint); },
                                    [this, generation](const EndpointCheck &check) {
                                        if (generation != m_speechEndpoint.generation) return;
                                        m_speechEndpoint.checking = false;
                                        m_speechEndpoint.result = check;
                                        emit changed();
                                    });
}

void LocalSetup::checkRefinementEndpoint(const RefinementSettings &settings)
{
    const auto generation = ++m_refinementEndpoint.generation;
    m_checkedRefinement = resolvedRefinementEndpoint(settings);
    m_refinementEndpoint.result = {};
    m_refinementEndpoint.checking = true;
    emit changed();
    runProviderProbe<EndpointCheck>(&m_providers, this,
                                    [settings] { return speecher::checkRefinementEndpoint(settings); },
                                    [this, generation](const EndpointCheck &check) {
                                        if (generation != m_refinementEndpoint.generation) return;
                                        m_refinementEndpoint.checking = false;
                                        m_refinementEndpoint.result = check;
                                        emit changed();
                                    });
}

QString LocalSetup::endpointStatus(const EndpointState &state)
{
    return state.checking ? QStringLiteral("Checking…") : state.result.message;
}

bool LocalSetup::runSettingsAction(const QString &rowId, const AppSettings &shown)
{
    if (rowId == QStringLiteral("speechEndpointTest")) {
        checkSpeechEndpoint(shown.speech.endpoint);
    } else if (rowId == QStringLiteral("refinementEndpointTest")) {
        checkRefinementEndpoint(shown.refinement);
    } else if (rowId == QStringLiteral("localRunnerDetect") || rowId == QStringLiteral("localModelsRunner")) {
        detectRunners();
    } else if (rowId == QStringLiteral("localModelFolder")) {
        QDir().mkpath(m_models.directory());
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_models.directory()));
    } else {
        return false;
    }
    return true;
}

LiveFacts LocalSetup::liveFacts() const
{
    return liveFacts(m_settings.snapshot());
}

LiveFacts LocalSetup::liveFacts(const AppSettings &draft) const
{
    LiveFacts facts;
    if (m_checkedSpeech == draft.speech.endpoint) {
        facts.speechEndpointStatus = endpointStatus(m_speechEndpoint);
        facts.speechEndpointModels = m_speechEndpoint.result.models;
    }
    if (m_checkedRefinement == resolvedRefinementEndpoint(draft.refinement)) {
        facts.refinementEndpointStatus = endpointStatus(m_refinementEndpoint);
        facts.refinementEndpointModels = m_refinementEndpoint.result.models;
    }
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
