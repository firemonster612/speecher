#include "app/LocalSetup.h"

#include "app/ProviderAvailability.h"
#include "core/ProviderChain.h"
#include "core/SettingsStore.h"
#include "core/settings/FallbackPresentation.h"
#include "core/settings/SettingsKeys.h"
#include "providers/LocalModelStore.h"
#include "providers/ProviderProbe.h"
#include "providers/ProviderRegistry.h"
#ifdef SPEECHER_WITH_LOCAL_SPEECH
#include "providers/LocalSpeechTranscriber.h"
#endif

#include <QDebug>
#include <QElapsedTimer>
#include <QScopeGuard>
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

// What a connection check depends on. The model is picked from its answer,
// so choosing one keeps the verdict and the list.
template <typename Endpoint>
Endpoint connection(Endpoint endpoint)
{
    endpoint.model.clear();
    return endpoint;
}

// The endpoint dictation may use, as the primary or a fallback, once it has a
// server to ask.
std::optional<SpeechEndpointSettings> speechEndpointInUse(const AppSettings &settings)
{
    const QStringList chain =
        providerChain(ProviderRole::Speech, settings.speech.providerId, settings.speech.fallbackProviderIds);
    if (!chain.contains(QStringLiteral("endpoint")) || settings.speech.endpoint.baseUrl.isEmpty()) {
        return std::nullopt;
    }
    return connection(settings.speech.endpoint);
}

std::optional<RefinementEndpoint> refinementEndpointInUse(const AppSettings &settings)
{
    const RefinementEndpoint endpoint = resolvedRefinementEndpoint(settings.refinement);
    const QStringList chain = providerChain(ProviderRole::Refinement, settings.refinement.providerId,
                                            settings.refinement.fallbackProviderIds);
    if (!chain.contains(QStringLiteral("endpoint")) || endpoint.apiBase.isEmpty()) {
        return std::nullopt;
    }
    return connection(endpoint);
}

constexpr int kEndpointCheckDelayMs = 800;

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

QString setupRefinementChoice(const QString &saved, const QStringList &ready, bool runnerFound,
                              bool explicitlyChosen)
{
    if (explicitlyChosen) return saved;
    const QString chosen = setupProviderChoice(saved, ready, false);
    if (!isSetupSignInProvider(chosen) || ready.contains(chosen)) return chosen;
    return runnerFound ? QStringLiteral("local") : QStringLiteral("none");
}

QString setupSpeechChoice(const QString &saved, const QStringList &ready, bool localOffered,
                          bool proxyAccountFound, bool explicitlyChosen)
{
    const QString chosen = setupProviderChoice(saved, ready, explicitlyChosen);
    if (explicitlyChosen || !localOffered || proxyAccountFound || !isSetupSignInProvider(chosen)
        || ready.contains(chosen)) {
        return chosen;
    }
    return QStringLiteral("local");
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

HardwareProfile runsOnProfile(const HardwareSummary &hardware, const LocalRunsOn &runsOn)
{
    HardwareProfile profile = hardware.profile;
    const bool anyBackend = runsOn.backend == QStringLiteral("auto");
    if (anyBackend && runsOn.deviceId.isEmpty()) {
        return profile;
    }
    const auto card = std::find_if(hardware.gpus.cbegin(), hardware.gpus.cend(),
                                   [&](const LocalSpeechEngine::Device &gpu) {
                                       return gpu.id == runsOn.deviceId && (anyBackend || gpu.kind == runsOn.backend);
                                   });
    if (runsOn.backend == QStringLiteral("cpu") || card == hardware.gpus.cend()) {
        profile.accelerator = HardwareProfile::Accelerator::Cpu;
        profile.gpuMemoryBytes = 0;
        return profile;
    }
    // Apple Silicon's GPU shares system RAM, so its budget is already right.
    if (profile.accelerator == HardwareProfile::Accelerator::AppleSilicon) {
        return profile;
    }
    if (card->type == LocalSpeechEngine::Device::Type::IntegratedGpu) {
        profile.accelerator = HardwareProfile::Accelerator::IntegratedGpu;
        profile.gpuMemoryBytes = 0;
    } else {
        profile.accelerator = HardwareProfile::Accelerator::DedicatedGpu;
        profile.gpuMemoryBytes = card->memoryTotalBytes;
    }
    return profile;
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
                        local.speedTestSeconds.insert(localSpeedTestKey(modelId, m_speedTestRunsOn), seconds);
                        m_settings.setLocalSpeechSettings(local);
                    } else {
                        m_speedTestErrors.insert(modelId, error);
                    }
                    emit changed();
                });
        connect(local, &LocalSpeechTranscriber::runsOnChanged, this, [this](const QString &description) {
            m_modelRunsOn = description;
            emit changed();
        });
    }
#endif
    // An endpoint someone is filling in is checked once they pause, without
    // pressing Test connection; one request per pause, not per keystroke.
    for (QTimer *delay : {&m_speechCheckDelay, &m_refinementCheckDelay}) {
        delay->setSingleShot(true);
        delay->setInterval(kEndpointCheckDelayMs);
    }
    connect(&m_speechCheckDelay, &QTimer::timeout, this,
            [this] { checkSpeechEndpoint(m_settings.dictationSnapshot().speech.endpoint); });
    connect(&m_refinementCheckDelay, &QTimer::timeout, this,
            [this] { checkRefinementEndpoint(m_settings.dictationSnapshot().refinement); });
    connect(&m_settings, &SettingsStore::snapshotApplied, this, [this](const AppSettings &previous) {
        const AppSettings current = m_settings.dictationSnapshot();
        // A new Runs on choice changes what fits and which speeds were
        // measured. The loaded model still runs on the old choice until the
        // next load says where the new one put it.
        if (current.speech.local.runsOn != previous.speech.local.runsOn) {
            m_modelRunsOn.clear();
            emit changed();
        }
        const auto reschedule = [](QTimer &delay, const auto &now, const auto &before) {
            if (now == before) return;
            if (now) delay.start();
            else delay.stop();
        };
        reschedule(m_speechCheckDelay, speechEndpointInUse(current), speechEndpointInUse(previous));
        reschedule(m_refinementCheckDelay, refinementEndpointInUse(current), refinementEndpointInUse(previous));
    });
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

bool LocalSetup::canRunAnyModel() const
{
    // Unknown is able: before the probe answers, and when a probe answered
    // with nothing (no memory reading), the machine is not declared too small.
    if (!m_hardwareKnown || m_hardware.profile.systemRamBytes == 0) {
        return true;
    }
    // Capability, not the moment's memory pressure: free RAM at probe time
    // says what fits right now, while this answer decides whether running
    // locally is offered at all, and the probe never runs again. Judge by
    // installed capacity, on the probe's placement or failing that the CPU.
    const auto capacity = [](HardwareProfile profile) {
        profile.availableRamBytes = profile.systemRamBytes;
        return profile;
    };
    return anyLocalModelFits(capacity(m_hardware.profile))
        || anyLocalModelFits(capacity(runsOnProfile(m_hardware, {QStringLiteral("cpu"), QString()})));
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
        parts << QStringLiteral("%1, %2").arg(gpu.description, localBackendName(gpu.kind));
    }
    parts << (profile.accelerator == HardwareProfile::Accelerator::DedicatedGpu
                  ? QStringLiteral("%1 memory, %2 on the graphics card")
                        .arg(gigabytesText(profile.systemRamBytes), gigabytesText(profile.gpuMemoryBytes))
                  : QStringLiteral("%1 memory").arg(gigabytesText(profile.systemRamBytes)));
    return parts.join(QStringLiteral(" · "));
}

const LocalModel &LocalSetup::suggestedModel() const
{
    return suggestedLocalModel(m_hardware.profile, m_settings.spokenLanguage());
}

std::optional<ModelFit> LocalSetup::fit(const LocalModel &model) const
{
    if (!m_hardwareKnown) {
        return std::nullopt;
    }
    return modelFit(model, runsOnProfile(m_hardware, m_settings.localSpeechSettings().runsOn));
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
    local.speedTestSeconds.removeIf([&model](const auto &result) {
        return result.key().startsWith(model.id + QLatin1Char('/'));
    });
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
    m_speedTestRunsOn = m_settings.localSpeechSettings().runsOn;
    m_speedTestErrors.remove(modelId);
    emit changed();
    local->runSpeedTest(modelId, m_speedTestRunsOn);
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
    const LocalSpeechSettings local = m_settings.localSpeechSettings();
    const auto it = local.speedTestSeconds.constFind(localSpeedTestKey(modelId, local.runsOn));
    return it == local.speedTestSeconds.cend() ? std::nullopt : std::optional<double>(*it);
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
    state.suggested = m_hardwareKnown && model.id == suggestedModel().id;
    state.tooLarge = fit(model) == ModelFit::TooLarge;
    state.cardFacts = QStringLiteral("%1\n%2\n%3% of words wrong on clear speech, %4% on everyday speech")
                          .arg(model.streams ? QStringLiteral("Words appear as you speak")
                                             : QStringLiteral("Text appears after you stop speaking"),
                               state.speedDetail)
                          .arg(model.librispeechCleanWer)
                          .arg(model.fleursEnglishWer);
    state.tableCells = {
        state.suggested ? model.name + QStringLiteral(" (suggested)") : model.name,
        downloadSizeText(model.sizeBytes),
        QStringLiteral("%1% / %2%").arg(model.librispeechCleanWer).arg(model.fleursEnglishWer),
        state.speedText,
        textShowsValue(model.streams),
        fitLabel(model),
    };
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
                                                m_runnersChecked = true;
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
    m_speechCheckDelay.stop();
    const auto generation = ++m_speechEndpoint.generation;
    m_checkedSpeech = connection(endpoint);
    m_speechEndpoint.result = {};
    m_speechEndpoint.checking = true;
    emit changed();
    runProviderProbe<EndpointCheck>(&m_providers, this,
                                    [endpoint] { return speecher::checkSpeechEndpoint(endpoint); },
                                    [this, generation](const EndpointCheck &check) {
                                        if (generation != m_speechEndpoint.generation) return;
                                        m_speechEndpoint.checking = false;
                                        m_speechEndpoint.result = check;
                                        // Only for the server that is saved, not one still being typed.
                                        SpeechEndpointSettings saved = m_settings.dictationSnapshot().speech.endpoint;
                                        if (saved.model.isEmpty() && !check.models.isEmpty()
                                            && connection(saved) == m_checkedSpeech) {
                                            saved.model = check.models.first();
                                            m_settings.setSpeechEndpointSettings(saved);
                                        }
                                        emit changed();
                                    });
}

void LocalSetup::checkRefinementEndpoint(const RefinementSettings &settings)
{
    m_refinementCheckDelay.stop();
    const auto generation = ++m_refinementEndpoint.generation;
    m_checkedRefinement = connection(resolvedRefinementEndpoint(settings));
    m_refinementEndpoint.result = {};
    m_refinementEndpoint.checking = true;
    emit changed();
    runProviderProbe<EndpointCheck>(&m_providers, this,
                                    [settings] { return speecher::checkRefinementEndpoint(settings); },
                                    [this, generation](const EndpointCheck &check) {
                                        if (generation != m_refinementEndpoint.generation) return;
                                        m_refinementEndpoint.checking = false;
                                        m_refinementEndpoint.result = check;
                                        const RefinementSettings saved = m_settings.dictationSnapshot().refinement;
                                        if (saved.endpoint.model.isEmpty() && !check.models.isEmpty()
                                            && connection(resolvedRefinementEndpoint(saved)) == m_checkedRefinement) {
                                            RefinementEndpointSettings endpoint = saved.endpoint;
                                            endpoint.model = check.models.first();
                                            m_settings.setRefinementEndpointSettings(endpoint);
                                        }
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
    } else if (rowId == QStringLiteral("localRunnerDetect")) {
        detectRunners();
    } else if (rowId == QStringLiteral("localModelFolder")) {
        QDir().mkpath(m_models.directory());
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_models.directory()));
    } else {
        return false;
    }
    return true;
}

std::optional<SetupFallbackOffer> LocalSetup::setupFallbackOffer(ProviderRole role) const
{
    const QString local = QStringLiteral("local");
    if (role == ProviderRole::Speech) {
        const LocalModel &model = suggestedModel();
        if (fit(model) != ModelFit::Fits || !localModelListensFor(model, m_settings.spokenLanguage())) {
            return std::nullopt;
        }
        return SetupFallbackOffer{local, model.id, model.name};
    }
    const RunnerChoice choice = runnerChoice();
    if (!choice.available || !choice.available->models.contains(choice.selection.model)) {
        return std::nullopt;
    }
    return SetupFallbackOffer{local, choice.selection.model, localRunnerName(choice.available->id)};
}

void LocalSetup::acceptSetupFallbackOffer(ProviderRole role)
{
    const std::optional<SetupFallbackOffer> offer = setupFallbackOffer(role);
    if (!offer) {
        return;
    }
    // A full chain, or the offer already the primary or a fallback, would drop
    // it; then nothing changes, not even the model or the runner it names.
    const AppSettings settings = m_settings.dictationSnapshot();
    const QStringList &saved =
        role == ProviderRole::Speech ? settings.speech.fallbackProviderIds : settings.refinement.fallbackProviderIds;
    const QStringList fallbacks = withFallbackAdded(settings, role, offer->providerId);
    if (fallbacks.size() <= saved.size()) {
        return;
    }
    if (role == ProviderRole::Speech) {
        m_settings.setSpeechFallbackProviders(fallbacks);
        chooseSpeechModel(offer->modelId);
        download(*findLocalModel(offer->modelId));
        return;
    }
    m_settings.setLocalRunnerSettings(runnerChoice().selection);
    m_settings.setRefinementFallbackProviders(fallbacks);
    emit changed();
}

void LocalSetup::setProviderAvailability(const ProviderAvailability &availability)
{
    m_availability = &availability;
    connect(&availability, &ProviderAvailability::changed, this, &LocalSetup::changed);
}

LiveFacts LocalSetup::liveFacts() const
{
    QElapsedTimer timer;
    timer.start();
    const AppSettings snapshot = m_settings.dictationSnapshot();
    qInfo() << "[DEBUG-win-slow] snapshot_ns" << timer.nsecsElapsed();
    return liveFacts(snapshot);
}

LiveFacts LocalSetup::liveFacts(const AppSettings &draft) const
{
    QElapsedTimer timer;
    timer.start();
    const auto timing = qScopeGuard([&] {
        qInfo() << "[DEBUG-win-slow] livefacts_ns" << timer.nsecsElapsed();
    });
    LiveFacts facts;
    if (m_checkedSpeech == connection(draft.speech.endpoint)) {
        facts.speechEndpointStatus = endpointStatus(m_speechEndpoint);
        facts.speechEndpointModels = m_speechEndpoint.result.models;
    }
    if (m_checkedRefinement == connection(resolvedRefinementEndpoint(draft.refinement))) {
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
    facts.runnersChecked = m_runnersChecked;
    if (m_availability) {
        m_availability->addTo(facts);
    }
    qint64 used = 0;
    for (const LocalModel &model : localModelCatalog()) {
        if (qEnvironmentVariableIsSet("DEBUG_SKIP_MODEL_SCAN")) break;
        if (m_models.isDownloaded(model)) {
            used += model.sizeBytes;
            facts.downloadedModels.append(model.id);
        }
    }
    facts.modelFolder = QStringLiteral("%1 · %2 used")
                            .arg(QDir::toNativeSeparators(m_models.directory()), downloadSizeText(used));
    for (const LocalSpeechEngine::Device &gpu : m_hardware.gpus) {
        facts.localGpus.append({gpu.kind, gpu.id, gpu.description});
    }
    facts.localModelRunsOn = m_modelRunsOn;
    return facts;
}

} // namespace speecher
