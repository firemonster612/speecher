#include "frontend/quick/host/QuickModels.h"

#include "app/ApplicationController.h"
#include "app/LocalSetup.h"
#include "app/ProviderAvailability.h"
#include "core/ProviderRatings.h"
#include "core/ProviderTable.h"
#include "core/SettingsStore.h"
#include "core/settings/SpokenLanguages.h"
#include "frontend/quick/host/QmlValues.h"
#include "providers/LocalModelStore.h"
#include "providers/ProviderRegistry.h"
#include "providers/TranscriptRefinementPrompt.h"

#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QHash>
#include <QUrl>

#include <algorithm>

namespace speecher {

namespace {

// The runners LocalSetup looks for, in the order it looks.
const QStringList kRunnerIds{QStringLiteral("ollama"), QStringLiteral("lmstudio"), QStringLiteral("llama-server")};

QString problemState(FallbackProblem problem)
{
    switch (problem) {
    case FallbackProblem::None:
        break;
    case FallbackProblem::Offline:
        return QStringLiteral("offline");
    case FallbackProblem::SignedOut:
        return QStringLiteral("signedOut");
    case FallbackProblem::NoModel:
        return QStringLiteral("noModel");
    case FallbackProblem::NoRunner:
        return QStringLiteral("noRunner");
    case FallbackProblem::NoServer:
        return QStringLiteral("noServer");
    case FallbackProblem::CheckFailed:
        return QStringLiteral("checkFailed");
    case FallbackProblem::SpokenLanguage:
        return QStringLiteral("language");
    }
    return {};
}

// A provider as the choice lists show it, judged as `speecher providers`
// judges it: unknown while what decides it, such as a sign-in, is unseen.
QVariantMap providerEntry(const ProviderReport &report)
{
    const QString problem = problemState(report.problemKind);
    return {
        {QStringLiteral("id"), report.id},
        {QStringLiteral("label"), report.label},
        {QStringLiteral("signsIn"), report.signsIn},
        {QStringLiteral("state"), !problem.isEmpty() ? problem
                                  : report.usable.value_or(false) ? QStringLiteral("ready")
                                                                  : QStringLiteral("unknown")},
        {QStringLiteral("problem"), problem.isEmpty() ? QString() : report.problem},
    };
}

QString measureName(RatingMeasure measure)
{
    switch (measure) {
    case RatingMeasure::Accuracy:
        return QStringLiteral("accuracy");
    case RatingMeasure::Quality:
        return QStringLiteral("quality");
    case RatingMeasure::Speed:
        break;
    }
    return QStringLiteral("speed");
}

QVariantList barList(const QList<Rating> &bars)
{
    QVariantList list;
    for (const Rating &bar : bars) {
        list.append(QVariantMap{{QStringLiteral("measure"), measureName(bar.measure)},
                                {QStringLiteral("value"), optionalNumber(bar.value)}});
    }
    return list;
}

QVariantMap ratings(ProviderRole role, const QStringList &ids, const HardwareProfile &hardware,
                    const AppSettings &settings)
{
    QVariantMap byId;
    for (const QString &id : ids) {
        if (const std::optional<ProviderRating> rating = providerRating(role, id, hardware, settings)) {
            byId.insert(id, QVariantMap{{QStringLiteral("bars"), barList(rating->bars)},
                                        {QStringLiteral("ratedModel"), rating->ratedModel}});
        }
    }
    return byId;
}

QString ratingName(ModelRating rating)
{
    switch (rating) {
    case ModelRating::Recommended:
        return QStringLiteral("recommended");
    case ModelRating::Good:
        return QStringLiteral("good");
    case ModelRating::Situational:
        return QStringLiteral("situational");
    case ModelRating::NotRecommended:
        break;
    }
    return QStringLiteral("notRecommended");
}

QString fitName(std::optional<ModelFit> fit)
{
    if (!fit) {
        return {};
    }
    switch (*fit) {
    case ModelFit::Fits:
        return QStringLiteral("fits");
    case ModelFit::Tight:
        return QStringLiteral("tight");
    case ModelFit::TooLarge:
        break;
    }
    return QStringLiteral("tooLarge");
}

QVariantMap catalogEntry(const LocalModel &model, const LocalSetup &local, const AppSettings &settings)
{
    const HardwareProfile &hardware = local.hardware().profile;
    const std::optional<SpeedEstimate> estimate =
        local.hardwareKnown() ? estimatedSpeed(model, hardware) : std::nullopt;
    return {
        {QStringLiteral("id"), model.id},
        {QStringLiteral("name"), model.name},
        {QStringLiteral("rating"), ratingName(model.rating)},
        {QStringLiteral("bestFor"), model.bestFor},
        {QStringLiteral("sizeBytes"), double(model.sizeBytes)},
        {QStringLiteral("fit"), fitName(local.fit(model, settings.speech.local))},
        {QStringLiteral("werClear"), model.librispeechCleanWer},
        {QStringLiteral("werEveryday"), model.fleursEnglishWer},
        {QStringLiteral("streams"), model.streams},
        {QStringLiteral("languages"), model.languages},
        {QStringLiteral("detectsLanguage"), model.detectsLanguage},
        {QStringLiteral("licence"), model.licence},
        {QStringLiteral("fileName"), model.fileName},
        {QStringLiteral("estimatedSeconds"),
         optionalNumber(estimate ? std::optional(estimate->secondsFor10sSpeech) : std::nullopt)},
        {QStringLiteral("measuredSeconds"), optionalNumber(measuredSpeedTestSeconds(settings.speech.local, model.id))},
        {QStringLiteral("bars"), barList(localModelRating(model, hardware, settings.speech.local))},
        {QStringLiteral("pros"), model.pros},
        {QStringLiteral("cons"), model.cons},
        {QStringLiteral("problem"), local.modelState(model, settings.speech).problem},
    };
}

QVariantMap endpointEntry(const QString &status, bool checking, bool ok, const QStringList &models)
{
    return {
        {QStringLiteral("checked"), !status.isEmpty()},
        {QStringLiteral("checking"), checking},
        {QStringLiteral("ok"), ok && !checking},
        {QStringLiteral("status"), checking ? QString() : status},
        {QStringLiteral("models"), models},
    };
}

QVariantMap computerEntry(const LocalSetup &local)
{
    if (!local.hardwareKnown()) {
        return {};
    }
    const HardwareSummary &hardware = local.hardware();
    const auto gpu = hardware.gpus.isEmpty() ? std::optional<LocalSpeechEngine::Device>()
                                             : std::optional(hardware.gpus.first());
    return {
        {QStringLiteral("chip"), hardware.profile.chipName},
        {QStringLiteral("threads"), hardware.cpuThreads},
        {QStringLiteral("memoryBytes"), double(hardware.profile.systemRamBytes)},
        // Only a dedicated card has memory of its own.
        {QStringLiteral("gpuMemoryBytes"),
         hardware.profile.accelerator == HardwareProfile::Accelerator::DedicatedGpu
             ? double(hardware.profile.gpuMemoryBytes) : 0.0},
        {QStringLiteral("gpu"), gpu ? gpu->description : QString()},
        {QStringLiteral("gpuBackend"), gpu ? localBackendName(gpu->kind) : QString()},
    };
}

QVariantMap fallbackOffer(const LocalSetup &local, ProviderRole role, const QStringList &primaries,
                          const AppSettings &settings)
{
    const std::optional<SetupFallbackOffer> offer = local.setupFallbackOffer(role, settings);
    if (!offer) {
        return {};
    }
    QStringList joins;
    for (const QString &primary : primaries) {
        if (local.setupFallbackOfferJoins(role, primary, settings)) {
            joins.append(primary);
        }
    }
    return {{QStringLiteral("modelId"), offer->modelId},
            {QStringLiteral("name"), offer->name},
            {QStringLiteral("joins"), joins}};
}

QVariantList namedModels(const QString &providerId)
{
    QVariantList models;
    for (const RefinementModel &model : refinementServiceModels(providerId)) {
        models.append(QVariantMap{{QStringLiteral("id"), model.id}, {QStringLiteral("label"), model.name}});
    }
    return models;
}

} // namespace

QuickModels::QuickModels(ApplicationController *controller, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
{
    LocalSetup *local = controller->localSetup();
    connect(local, &LocalSetup::changed, this, &QuickModels::refresh);
    connect(local, &LocalSetup::changed, this, &QuickModels::refreshDownloads);
    connect(local, &LocalSetup::changed, this, &QuickModels::pullChanged);
    connect(local, &LocalSetup::pullProgress, this, &QuickModels::pullChanged);
    connect(controller->settings(), &SettingsStore::snapshotApplied, this, &QuickModels::refresh);
    // LocalSetup records the bytes from this signal, so it reads them after.
    connect(controller->localModelStore(), &LocalModelStore::downloadProgress, this, &QuickModels::refreshDownloads,
            Qt::QueuedConnection);
    refresh();
    refreshDownloads();
}

void QuickModels::refresh()
{
    const LocalSetup &local = *m_controller->localSetup();
    const ProviderRegistry &registry = *m_controller->providerRegistry();
    const ProviderAvailability &availability = *m_controller->providerAvailability();
    const AppSettings settings = m_controller->settings()->dictationSnapshot();
    const LiveFacts facts = local.liveFacts(settings);
    const HardwareProfile &hardware = local.hardware().profile;
    const QStringList speechIds = providerIds(registry.speechProviders());
    const QStringList refinementIds = providerIds(registry.refinementProviders());

    QHash<QString, bool> readsScreenshots;
    for (const ProviderDescriptor &provider : registry.refinementProviders()) {
        readsScreenshots.insert(provider.id, provider.supportsScreenshotContext);
    }
    QVariantList speechServices;
    QVariantList refinementProviders;
    for (const ProviderReport &report :
         providerReports(settings, facts, speechIds, refinementIds, registry.labels())) {
        QVariantMap entry = providerEntry(report);
        if (report.role == ProviderRole::Speech) {
            const SpeechSettings speech = chainSpeechSettings(settings.speech, report.id);
            QStringList languages = spokenLanguages(speech);
            const bool detects = languages.removeAll(QLatin1String(kAutomaticSpokenLanguage)) > 0;
            entry.insert(QStringLiteral("languages"), languages);
            entry.insert(QStringLiteral("detects"), detects);
            entry.insert(QStringLiteral("spokenLanguage"), speech.language);
            entry.insert(QStringLiteral("signInChecking"), availability.checkingSignIn(report.id));
            entry.insert(QStringLiteral("signInCheckFailure"), availability.signInCheckFailure(report.id));
            speechServices.append(entry);
            continue;
        }
        entry.insert(QStringLiteral("readsScreenshots"), readsScreenshots.value(report.id));
        refinementProviders.append(entry);
    }

    QVariantList accelerationOptions;
    for (const LocalAccelerationChoice &choice : localAccelerationChoices(facts.localGpus, settings.speech.local.runsOn)) {
        accelerationOptions.append(QVariantMap{{QStringLiteral("id"), choice.backend},
                                               {QStringLiteral("label"), localBackendName(choice.backend)},
                                               {QStringLiteral("available"), choice.available}});
    }
    QVariantList graphicsCards;
    for (const LocalGpu &card : localGraphicsCardChoices(facts.localGpus, settings.speech.local.runsOn)) {
        graphicsCards.append(
            QVariantMap{{QStringLiteral("id"), card.deviceId}, {QStringLiteral("label"), card.description}});
    }

    QVariantList catalog;
    QString inUse;
    QStringList speedTests;
    double folderBytes = 0;
    for (const LocalModel &model : localModelCatalog()) {
        catalog.append(catalogEntry(model, local, settings));
        if (local.modelState(model, settings.speech).inUse) {
            inUse = model.id;
        }
        if (local.speedTestRunning(model.id)) {
            speedTests.append(model.id);
        }
        if (facts.downloadedModels.contains(model.id)) {
            folderBytes += double(model.sizeBytes);
        }
    }

    QVariantList runners;
    for (const QString &id : kRunnerIds) {
        const auto found = std::find_if(facts.runners.cbegin(), facts.runners.cend(),
                                        [&id](const FoundRunner &runner) { return runner.id == id; });
        const bool running = found != facts.runners.cend();
        runners.append(QVariantMap{{QStringLiteral("id"), id},
                                   {QStringLiteral("label"), running ? found->label : localRunnerName(id)},
                                   {QStringLiteral("running"), running},
                                   {QStringLiteral("models"), facts.runnerModels.value(id)}});
    }
    const std::optional<CleanupModel> cleanup = local.hardwareKnown() ? local.suggestedCleanupModel() : std::nullopt;
    const QVariantMap cleanupSuggestion =
        cleanup ? QVariantMap{{QStringLiteral("tag"), cleanup->ollamaTag},
                              {QStringLiteral("name"), cleanup->name},
                              {QStringLiteral("sizeBytes"), double(cleanup->sizeBytes)}}
                : QVariantMap();

    bool dirty = false;
    const auto assign = [&dirty](auto &member, const auto &value) {
        if (member != value) {
            member = value;
            dirty = true;
        }
    };
    assign(m_speechServices, speechServices);
    assign(m_refinementProviders, refinementProviders);
    assign(m_speechRatings, ratings(ProviderRole::Speech, speechIds, hardware, settings));
    assign(m_refinementRatings, ratings(ProviderRole::Refinement, refinementIds, hardware, settings));
    assign(m_spokenLanguageProblem,
           speecher::spokenLanguageProblem(settings.speech, registry.speechProviderLabel(settings.speech.providerId)));
    assign(m_hardwareKnown, local.hardwareKnown());
    assign(m_computer, computerEntry(local));
    assign(m_accelerationOptions, accelerationOptions);
    assign(m_graphicsCards, graphicsCards);
    assign(m_runningOn, facts.localModelRunsOn);
    assign(m_modelFolder, QDir::toNativeSeparators(m_controller->localModelStore()->directory()));
    assign(m_modelFolderBytes, folderBytes);
    assign(m_catalog, catalog);
    assign(m_downloaded, facts.downloadedModels);
    assign(m_speedTests, speedTests);
    assign(m_localModelChoice, local.speechModelChoice(settings).id);
    assign(m_suggestedModel, local.hardwareKnown() ? local.suggestedModel(settings).id : QString());
    assign(m_inUse, inUse);
    assign(m_speechEndpoint, endpointEntry(facts.speechEndpointStatus, facts.speechEndpointChecking,
                                           facts.speechEndpointOk, facts.speechEndpointModels));
    assign(m_refinementEndpoint, endpointEntry(facts.refinementEndpointStatus, facts.refinementEndpointChecking,
                                               facts.refinementEndpointOk, facts.refinementEndpointModels));
    assign(m_runners, runners);
    assign(m_detectingRunners, facts.detectingRunners);
    assign(m_runnersChecked, facts.runnersChecked);
    assign(m_cleanupSuggestion, cleanupSuggestion);
    assign(m_offerPull, local.runnerChoice(settings).offerPull);
    assign(m_fallbackOffers,
           QVariantMap({{QStringLiteral("speech"), fallbackOffer(local, ProviderRole::Speech, speechIds, settings)},
                        {QStringLiteral("refinement"),
                         fallbackOffer(local, ProviderRole::Refinement, refinementIds, settings)}}));
    if (dirty) {
        emit changed();
    }
}

void QuickModels::refreshDownloads()
{
    const LocalSetup &local = *m_controller->localSetup();
    QVariantMap downloads;
    for (const LocalModel &model : localModelCatalog()) {
        if (const auto progress = local.downloadProgress(model.id)) {
            const auto [received, total] = *progress;
            downloads.insert(model.id, total > 0 ? double(received) / double(total) : 0.0);
        }
    }
    if (downloads != m_downloads) {
        m_downloads = downloads;
        emit downloadsChanged();
    }
}

QVariantList QuickModels::openAiModels() const
{
    return namedModels(providerKey(ProviderId::OpenAi));
}

QVariantList QuickModels::anthropicModels() const
{
    return namedModels(providerKey(ProviderId::Anthropic));
}

QVariantMap QuickModels::pull() const
{
    const LocalSetup::Pull pull = m_controller->localSetup()->pull();
    return {
        {QStringLiteral("tag"), pull.tag},
        {QStringLiteral("completedBytes"), double(pull.completedBytes)},
        {QStringLiteral("totalBytes"), double(pull.totalBytes)},
        {QStringLiteral("running"), pull.running},
        {QStringLiteral("error"), pull.error},
    };
}

QString QuickModels::builtInSystemPrompt() const
{
    return builtInDictationSystemPrompt();
}

void QuickModels::checkComputer()
{
    m_controller->localSetup()->probeHardware();
}

void QuickModels::checkSignIns()
{
    m_controller->providerAvailability()->checkSpeechSignIns(*m_controller->providerRegistry(),
                                                             m_controller->settings()->dictationSnapshot());
}

QVariantMap QuickModels::service(const QString &id) const
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("label"), m_controller->providerRegistry()->speechProviderLabel(id)}};
}

bool QuickModels::supportsUltrafast(const QString &model) const
{
    return openAiModelSupportsUltrafast(model);
}

void QuickModels::download(const QString &id)
{
    if (const LocalModel *model = findLocalModel(id)) {
        m_controller->localSetup()->download(*model);
    }
}

void QuickModels::cancel(const QString &id)
{
    m_controller->localSetup()->cancelDownload(id);
}

void QuickModels::remove(const QString &id)
{
    const LocalModel *model = findLocalModel(id);
    if (model && !m_controller->localSetup()->removeModel(*model)) {
        qWarning().noquote() << "Models.remove: a file of" << id << "stayed in" << m_modelFolder;
    }
}

void QuickModels::use(const QString &id)
{
    if (!findLocalModel(id)) {
        qWarning().noquote() << "Models.use:" << id << "is not a Local Model";
        return;
    }
    // The store drops Local Model from the fallbacks once it is the primary.
    m_controller->settings()->edit([](AppSettings &draft) {
        draft.speech.providerId = providerKey(ProviderId::Local);
        return true;
    });
    m_controller->localSetup()->chooseSpeechModel(id);
}

void QuickModels::testSpeed(const QString &id)
{
    m_controller->localSetup()->runSpeedTest(id);
}

void QuickModels::openModelFolder()
{
    const QString folder = m_controller->localModelStore()->directory();
    QDir().mkpath(folder);
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

void QuickModels::testSpeechEndpoint()
{
    m_controller->localSetup()->checkSpeechEndpoint(m_controller->settings()->dictationSnapshot().speech.endpoint);
}

void QuickModels::testRefinementEndpoint()
{
    m_controller->localSetup()->checkRefinementEndpoint(m_controller->settings()->dictationSnapshot().refinement);
}

void QuickModels::detectRunners()
{
    m_controller->localSetup()->detectRunners();
}

void QuickModels::pullCleanupModel()
{
    LocalSetup *local = m_controller->localSetup();
    if (const std::optional<CleanupModel> model = local->suggestedCleanupModel()) {
        local->pullCleanupModel(model->ollamaTag);
    }
}

void QuickModels::cancelPull()
{
    m_controller->localSetup()->cancelPull();
}

void QuickModels::acceptFallbackOffer(const QString &role, const QString &primary)
{
    m_controller->localSetup()->acceptSetupFallbackOffer(
        role == QLatin1String("refinement") ? ProviderRole::Refinement : ProviderRole::Speech, primary);
}

} // namespace speecher
