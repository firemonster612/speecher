#include "app/LocalSetup.h"

#include "app/ProviderAvailability.h"
#include "core/ProviderChain.h"
#include "core/ProviderTable.h"
#include "core/SettingsStore.h"
#include "core/ProviderReadiness.h"
#include "core/settings/SettingsKeys.h"
#include "providers/LocalModelStore.h"
#include "providers/ProviderProbe.h"
#include "providers/ProviderRegistry.h"
#ifdef SPEECHER_WITH_LOCAL_SPEECH
#include "providers/LocalSpeechTranscriber.h"
#endif

#include <QDir>
#include <QTimer>

#include <algorithm>

namespace speecher {
namespace {

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
    if (!chain.contains(providerKey(ProviderId::Endpoint)) || settings.speech.endpoint.baseUrl.isEmpty()) {
        return std::nullopt;
    }
    return connection(settings.speech.endpoint);
}

std::optional<RefinementEndpoint> refinementEndpointInUse(const AppSettings &settings)
{
    const RefinementEndpoint endpoint = resolvedRefinementEndpoint(settings.refinement);
    const QStringList chain = providerChain(ProviderRole::Refinement, settings.refinement.providerId,
                                            settings.refinement.fallbackProviderIds);
    if (!chain.contains(providerKey(ProviderId::Endpoint)) || endpoint.apiBase.isEmpty()) {
        return std::nullopt;
    }
    return connection(endpoint);
}

constexpr int kEndpointCheckDelayMs = 800;

} // namespace

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

QList<LocalAccelerationChoice> localAccelerationChoices(const QList<LocalGpu> &gpus, const LocalRunsOn &chosen)
{
    QList<LocalAccelerationChoice> choices;
    for (const LocalBackend &backend : localBackends) {
        const QString kind = QString::fromLatin1(backend.kind);
        const bool gpuBackend = kind != QStringLiteral("auto") && kind != QStringLiteral("cpu");
        const bool present =
            std::any_of(gpus.cbegin(), gpus.cend(), [&kind](const LocalGpu &gpu) { return gpu.backend == kind; });
        if (!gpuBackend || present) {
            choices.append({kind});
        }
    }
    if (std::none_of(choices.cbegin(), choices.cend(),
                     [&chosen](const LocalAccelerationChoice &choice) { return choice.backend == chosen.backend; })) {
        choices.append({chosen.backend, false});
    }
    return choices;
}

QList<LocalGpu> localGraphicsCardChoices(const QList<LocalGpu> &gpus, const LocalRunsOn &chosen)
{
    if (chosen.backend == QStringLiteral("cpu")) {
        return {};
    }
    // Automatic names no card of its own, except one saved before
    // acceleration could be chosen: then the card decides the backend.
    const bool automatic = chosen.backend == QStringLiteral("auto");
    if (automatic && chosen.deviceId.isEmpty()) {
        return {};
    }
    QList<LocalGpu> cards;
    const auto listed = [&cards](const QString &deviceId) {
        return std::any_of(cards.cbegin(), cards.cend(), [&deviceId](const LocalGpu &card) {
            return card.deviceId == deviceId;
        });
    };
    for (const LocalGpu &gpu : gpus) {
        const bool matches = automatic ? gpu.deviceId == chosen.deviceId : gpu.backend == chosen.backend;
        if (matches && !listed(gpu.deviceId)) {
            cards.append(gpu);
        }
    }
    if (!chosen.deviceId.isEmpty() && !listed(chosen.deviceId)) {
        cards.append({chosen.backend, chosen.deviceId, QString()});
    }
    return cards;
}

LocalRunsOn localRunsOnFor(const QList<LocalGpu> &gpus, const QString &backend)
{
    LocalRunsOn runsOn{backend, QString()};
    const QList<LocalGpu> cards = localGraphicsCardChoices(gpus, runsOn);
    runsOn.deviceId = cards.isEmpty() ? QString() : cards.first().deviceId;
    return runsOn;
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
        m_settings.edit([this](AppSettings &settings) {
            settings.refinement.localRunner.runner = QStringLiteral("ollama");
            settings.refinement.localRunner.model = ollamaListedName(m_pull.tag);
            return true;
        });
        detectRunners();
    });
    connect(&m_ollamaPull, &OllamaPull::failed, this, [this](const QString &message) {
        m_pull.running = false;
        m_pull.error = message;
        emit changed();
    });
#ifdef SPEECHER_WITH_LOCAL_SPEECH
    if (auto *local = qobject_cast<LocalSpeechTranscriber *>(
            m_providers.speechProvider(providerKey(ProviderId::Local)))) {
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
                        m_settings.edit([&](AppSettings &settings) {
                            settings.speech.local.speedTestSeconds.insert(
                                localSpeedTestKey(modelId, m_speedTestRunsOn), seconds);
                            return true;
                        });
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

const HardwareSummary &LocalSetup::hardware() const
{
    return m_hardware;
}

const LocalModel &LocalSetup::suggestedModel(const AppSettings &settings) const
{
    return suggestedLocalModel(m_hardware.profile, settings.speech.language);
}

std::optional<ModelFit> LocalSetup::fit(const LocalModel &model, const AppSettings &settings) const
{
    if (!m_hardwareKnown) {
        return std::nullopt;
    }
    return modelFit(model, runsOnProfile(m_hardware, settings.speech.local.runsOn));
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
    m_settings.edit([this, &model](AppSettings &settings) {
        LocalSpeechSettings &local = settings.speech.local;
        local.speedTestSeconds.removeIf([&model](const auto &result) {
            return result.key().startsWith(model.id + QLatin1Char('/'));
        });
        // Dictation moves to another downloaded model rather than a missing
        // file. With none left it stays, and dictating says to download one.
        if (local.modelId == model.id) {
            for (const LocalModel &other : localModelCatalog()) {
                if (m_models.isDownloaded(other)) {
                    local.modelId = other.id;
                    break;
                }
            }
        }
        return true;
    });
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

void LocalSetup::runSpeedTest(const QString &modelId)
{
#ifdef SPEECHER_WITH_LOCAL_SPEECH
    auto *local = qobject_cast<LocalSpeechTranscriber *>(m_providers.speechProvider(providerKey(ProviderId::Local)));
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
    m_speedTestRunsOn = m_settings.dictationSnapshot().speech.local.runsOn;
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

std::optional<double> LocalSetup::measuredSeconds(const QString &modelId, const AppSettings &settings) const
{
    return measuredSpeedTestSeconds(settings.speech.local, modelId);
}

const LocalModel &LocalSetup::speechModelChoice(const AppSettings &settings) const
{
    const LocalSpeechSettings &saved = settings.speech.local;
    const auto *model = findLocalModel(saved.modelId);
    return saved.modelChosen && model ? *model : suggestedModel(settings);
}

void LocalSetup::initializeSpeechModel()
{
    const AppSettings settings = m_settings.dictationSnapshot();
    if (!m_hardwareKnown || settings.speech.local.modelChosen) return;
    // The general settings setter treats a changed model as a user choice.
    // A hardware default must remain replaceable until the person chooses.
    m_settings.raw().setValue(SettingsKeys::LocalModel, suggestedModel(settings).id);
    m_settings.raw().setValue(SettingsKeys::LocalModelChosen, false);
}

void LocalSetup::chooseSpeechModel(const QString &id)
{
    if (!findLocalModel(id)) return;
    m_settings.edit([&id](AppSettings &settings) {
        settings.speech.local.modelId = id;
        settings.speech.local.modelChosen = true;
        return true;
    });
    emit changed();
}

RunnerChoice LocalSetup::runnerChoice(const AppSettings &settings) const
{
    return resolveRunnerChoice(settings.refinement.localRunner, m_runners, suggestedCleanupModel());
}

void LocalSetup::initializeRunner()
{
    const AppSettings settings = m_settings.dictationSnapshot();
    const LocalRunnerSettings &saved = settings.refinement.localRunner;
    if (!saved.runner.isEmpty() || !saved.model.isEmpty()) return;
    const auto choice = runnerChoice(settings);
    if (!choice.available) return;
    m_settings.edit([&choice](AppSettings &settings) {
        settings.refinement.localRunner = choice.selection;
        return true;
    });
}

LocalSetup::ModelState LocalSetup::modelState(const LocalModel &model, std::optional<SpeechSettings> draft) const
{
    const SpeechSettings speech = draft ? *draft : m_settings.dictationSnapshot().speech;
    ModelState state;
    state.inUse = !m_models.isDownloading(model.id) && m_models.isDownloaded(model)
        && parseProviderId(speech.providerId) == ProviderId::Local && speech.local.modelId == model.id;
    state.problem = m_downloadErrors.value(model.id, m_speedTestErrors.value(model.id));
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

void LocalSetup::cancelPull()
{
    if (!m_pull.running) {
        return;
    }
    // The pull's reply goes quietly, so nothing else reports the end.
    m_ollamaPull.cancel();
    m_pull.running = false;
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
                                        const SpeechEndpointSettings saved = m_settings.dictationSnapshot().speech.endpoint;
                                        if (saved.model.isEmpty() && !check.models.isEmpty()
                                            && connection(saved) == m_checkedSpeech) {
                                            m_settings.edit([&check](AppSettings &settings) {
                                                settings.speech.endpoint.model = check.models.first();
                                                return true;
                                            });
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
                                            m_settings.edit([&check](AppSettings &settings) {
                                                settings.refinement.endpoint.model = check.models.first();
                                                return true;
                                            });
                                        }
                                        emit changed();
                                    });
}

QString LocalSetup::endpointStatus(const EndpointState &state)
{
    return state.checking ? QStringLiteral("Checking…") : state.result.message;
}

std::optional<SetupFallbackOffer> LocalSetup::setupFallbackOffer(ProviderRole role, const AppSettings &settings) const
{
    const QString local = providerKey(ProviderId::Local);
    if (role == ProviderRole::Speech) {
        const LocalModel &model = suggestedModel(settings);
        if (fit(model, settings) != ModelFit::Fits || !localModelListensFor(model, settings.speech.language)) {
            return std::nullopt;
        }
        return SetupFallbackOffer{local, model.id, model.name};
    }
    const RunnerChoice choice = runnerChoice(settings);
    if (!choice.available || !choice.available->models.contains(choice.selection.model)) {
        return std::nullopt;
    }
    return SetupFallbackOffer{local, choice.selection.model, localRunnerName(choice.available->id)};
}

std::optional<QStringList> LocalSetup::fallbacksWithSetupOffer(ProviderRole role, const QString &primary,
                                                               const AppSettings &settings) const
{
    const std::optional<SetupFallbackOffer> offer = setupFallbackOffer(role, settings);
    if (!offer) {
        return std::nullopt;
    }
    // A full chain, or the offer already the primary or a fallback, would drop it.
    const QStringList kept = normalizedFallbackProviders(role, primary,
                                                         role == ProviderRole::Speech
                                                             ? settings.speech.fallbackProviderIds
                                                             : settings.refinement.fallbackProviderIds);
    const QStringList fallbacks = normalizedFallbackProviders(role, primary, QStringList(kept) << offer->providerId);
    if (fallbacks.size() <= kept.size()) {
        return std::nullopt;
    }
    return fallbacks;
}

bool LocalSetup::setupFallbackOfferJoins(ProviderRole role, const QString &primary,
                                         const AppSettings &settings) const
{
    return fallbacksWithSetupOffer(role, primary, settings).has_value();
}

void LocalSetup::acceptSetupFallbackOffer(ProviderRole role, const QString &primary)
{
    // Where the chain would drop the offer, nothing changes, not even the
    // model or the runner it names.
    const AppSettings settings = m_settings.dictationSnapshot();
    const std::optional<QStringList> fallbacks = fallbacksWithSetupOffer(role, primary, settings);
    if (!fallbacks) {
        return;
    }
    const SetupFallbackOffer offer = *setupFallbackOffer(role, settings);
    if (role == ProviderRole::Speech) {
        m_settings.edit([&](AppSettings &draft) {
            draft.speech.providerId = primary;
            draft.speech.fallbackProviderIds = *fallbacks;
            return true;
        });
        chooseSpeechModel(offer.modelId);
        download(*findLocalModel(offer.modelId));
        return;
    }
    const LocalRunnerSettings runner = runnerChoice(settings).selection;
    m_settings.edit([&](AppSettings &draft) {
        draft.refinement.localRunner = runner;
        draft.refinement.providerId = primary;
        draft.refinement.fallbackProviderIds = *fallbacks;
        return true;
    });
    emit changed();
}

void LocalSetup::setProviderAvailability(const ProviderAvailability &availability)
{
    m_availability = &availability;
    connect(&availability, &ProviderAvailability::changed, this, &LocalSetup::changed);
}

LiveFacts LocalSetup::liveFacts() const
{
    return liveFacts(m_settings.dictationSnapshot());
}

LiveFacts LocalSetup::liveFacts(const AppSettings &draft) const
{
    LiveFacts facts;
    if (m_checkedSpeech == connection(draft.speech.endpoint)) {
        facts.speechEndpointStatus = endpointStatus(m_speechEndpoint);
        facts.speechEndpointModels = m_speechEndpoint.result.models;
        facts.speechEndpointChecking = m_speechEndpoint.checking;
        facts.speechEndpointOk = m_speechEndpoint.result.ok;
    }
    if (m_checkedRefinement == connection(resolvedRefinementEndpoint(draft.refinement))) {
        facts.refinementEndpointStatus = endpointStatus(m_refinementEndpoint);
        facts.refinementEndpointModels = m_refinementEndpoint.result.models;
        facts.refinementEndpointChecking = m_refinementEndpoint.checking;
        facts.refinementEndpointOk = m_refinementEndpoint.result.ok;
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
