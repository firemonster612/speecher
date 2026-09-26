#include "providers/LocalRunner.h"
#include "providers/EndpointRequest.h"

#include "providers/BlockingHttp.h"
#include "providers/ChatCompletionsRefiner.h"
#include "providers/CustomEndpoints.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace speecher {
namespace {

HttpResult get(const QString &url, int timeoutMs)
{
    return blockingGet(endpointRequest(QUrl(url)), timeoutMs);
}

QJsonObject jsonObject(const HttpResult &result)
{
    return result.status == 200 ? QJsonDocument::fromJson(result.body).object() : QJsonObject{};
}

struct Runner {
    const char *id;
    const char *name;
    const char *origin;
    std::optional<DetectedRunner> (*probe)(const QString &origin, int timeoutMs);
};

// Ollama first: detectLocalRunners lists in this order.
const Runner runners[] = {
    {"ollama", "Ollama", "http://127.0.0.1:11434", &probeOllama},
    {"lmstudio", "LM Studio", "http://127.0.0.1:1234", &probeLmStudio},
    {"llama-server", "llama-server", "http://127.0.0.1:8080", &probeLlamaServer},
};

// Unknown ids read as Ollama, as LocalRunnerSettings stores them.
const Runner &runnerFor(const QString &runnerId)
{
    for (const Runner &runner : runners) {
        if (runnerId == QLatin1String(runner.id)) return runner;
    }
    return runners[0];
}

DetectedRunner detected(const QString &runnerId, const QString &version, const QString &origin,
                        const QStringList &models)
{
    const Runner &runner = runnerFor(runnerId);
    return {QString::fromLatin1(runner.id), QString::fromLatin1(runner.name), version,
            origin + QStringLiteral("/v1"), models};
}

QString runnerName(const QString &runnerId)
{
    return QString::fromLatin1(runnerFor(runnerId).name);
}

} // namespace

QString localRunnerOrigin(const QString &runnerId)
{
    return QString::fromLatin1(runnerFor(runnerId).origin);
}

QString ollamaListedName(const QString &model)
{
    return model.section(QLatin1Char('/'), -1).contains(QLatin1Char(':')) ? model : model + QStringLiteral(":latest");
}

std::optional<DetectedRunner> probeOllama(const QString &origin, int timeoutMs)
{
    // The root's fixed banner is Ollama's alone; the impostors serve a web UI
    // there.
    const HttpResult root = get(origin + QStringLiteral("/"), timeoutMs);
    if (root.status != 200 || root.body.trimmed() != "Ollama is running") return std::nullopt;
    const QString version = jsonObject(get(origin + QStringLiteral("/api/version"), timeoutMs))
                                .value(QStringLiteral("version")).toString();
    const HttpResult tags = get(origin + QStringLiteral("/api/tags"), timeoutMs);
    return detected(QStringLiteral("ollama"), version, origin,
                    tags.status == 200 ? modelIdsFromListing(tags.body) : QStringList{});
}

std::optional<DetectedRunner> probeLmStudio(const QString &origin, int timeoutMs)
{
    // LM Studio's native listings type each model, which OpenAI-style ones
    // do not: /api/v1/models (0.4.0 and later) keys them by "key", the older
    // /api/v0/models (0.3.6 and later) by "id" with "compatibility_type".
    struct Listing {
        const char *path;
        const char *array;
        const char *idField;
        const char *marker;
    };
    for (const Listing &listing : {Listing{"/api/v1/models", "models", "key", "key"},
                                   Listing{"/api/v0/models", "data", "id", "compatibility_type"}}) {
        const QJsonArray entries = jsonObject(get(origin + QLatin1String(listing.path), timeoutMs))
                                       .value(QLatin1String(listing.array)).toArray();
        if (entries.isEmpty() || !entries.first().toObject().contains(QLatin1String(listing.marker))) {
            continue;
        }
        QStringList models;
        for (const QJsonValue &entry : entries) {
            const QJsonObject model = entry.toObject();
            const QString type = model.value(QStringLiteral("type")).toString();
            if (type == QStringLiteral("llm") || type == QStringLiteral("vlm")) {
                models << model.value(QLatin1String(listing.idField)).toString();
            }
        }
        return detected(QStringLiteral("lmstudio"), {}, origin, models);
    }
    return std::nullopt;
}

std::optional<DetectedRunner> probeLlamaServer(const QString &origin, int timeoutMs)
{
    // whisper.cpp's server shares the port and /health but answers it with
    // {"status":...}; llama-server says "Loading model" in an error object
    // until its model is ready, and /props with build_info after that.
    // KoboldCpp serves a /props without build_info.
    const HttpResult health = get(origin + QStringLiteral("/health"), timeoutMs);
    const QJsonObject healthError = QJsonDocument::fromJson(health.body).object()
                                        .value(QStringLiteral("error")).toObject();
    if (health.status == 503
        && healthError.value(QStringLiteral("message")).toString() == QStringLiteral("Loading model")) {
        return detected(QStringLiteral("llama-server"), {}, origin, {});
    }
    if (health.status != 200) return std::nullopt;
    const QJsonObject props = jsonObject(get(origin + QStringLiteral("/props"), timeoutMs));
    const QString build = props.value(QStringLiteral("build_info")).toString();
    if (build.isEmpty()) return std::nullopt;
    const QString modelPath = props.value(QStringLiteral("model_path")).toString();
    return detected(QStringLiteral("llama-server"), build, origin,
                    modelPath.isEmpty() ? QStringList{} : QStringList{QFileInfo(modelPath).fileName()});
}

QList<DetectedRunner> detectLocalRunners(int timeoutMs)
{
    QList<DetectedRunner> found;
    for (const Runner &runner : runners) {
        if (std::optional<DetectedRunner> answer = runner.probe(QString::fromLatin1(runner.origin), timeoutMs)) {
            found << *answer;
        }
    }
    return found;
}

CleanupHardware cleanupHardwareFor(const HardwareProfile &hardware)
{
    // Gemma 4 E4B is 5.3 GB before its context; a card that cannot hold it
    // spills to system memory and runs at the processor's pace.
    constexpr quint64 largeCleanupModelGpuBytes = quint64(8) << 30;
    switch (hardware.accelerator) {
    case HardwareProfile::Accelerator::Cpu:
        return CleanupHardware::Cpu;
    case HardwareProfile::Accelerator::IntegratedGpu:
        return CleanupHardware::IntegratedGpu;
    case HardwareProfile::Accelerator::DedicatedGpu:
        return hardware.gpuMemoryBytes >= largeCleanupModelGpuBytes ? CleanupHardware::DedicatedGpu
                                                                     : CleanupHardware::Cpu;
    case HardwareProfile::Accelerator::AppleSilicon:
        break;
    }
    if (hardware.chipName.contains(QStringLiteral("Max")) || hardware.chipName.contains(QStringLiteral("Ultra"))) {
        return CleanupHardware::AppleMax;
    }
    return hardware.chipName.contains(QStringLiteral("Pro")) ? CleanupHardware::ApplePro
                                                             : CleanupHardware::AppleBase;
}

std::optional<CleanupModel> suggestedCleanupModel(CleanupHardware hardware)
{
    // Seconds for a 150-word dictation with the model loaded, from
    // notes-llm.md's latency table: the ~1B column for the small model and,
    // as the approved prototype does, the 3-4B column for Gemma 4 E4B. Rows:
    // 8-core Zen 4, Radeon 780M, RTX 3060, M1, M3 Pro, M4 Max.
    struct Latency {
        double small;
        double large;
    };
    const Latency latency = [hardware]() -> Latency {
        switch (hardware) {
        case CleanupHardware::Cpu: return {2.5, 8.4};
        case CleanupHardware::IntegratedGpu: return {1.6, 5.3};
        case CleanupHardware::DedicatedGpu: return {0.4, 1.4};
        case CleanupHardware::AppleBase: return {2.3, 7.8};
        case CleanupHardware::ApplePro: return {1.1, 3.5};
        case CleanupHardware::AppleMax: break;
        }
        return {0.4, 1.3};
    }();
    if (latency.large <= 2.0) {
        return CleanupModel{QStringLiteral("gemma4:e4b"), QStringLiteral("Gemma 4 E4B"), 5'300'000'000};
    }
    if (latency.small <= 3.0) {
        return CleanupModel{QStringLiteral("LiquidAI/lfm2.5-1.2b-instruct"), QStringLiteral("LFM2.5 1.2B"),
                            730'000'000};
    }
    return std::nullopt;
}

LocalRunnerRefiner::LocalRunnerRefiner(QObject *parent)
    : TranscriptRefiner(parent)
    , m_chat(new ChatCompletionsRefiner(QStringLiteral("Local model"),
                                        ChatCompletionsRefiner::Audience::SmallLocalModel, this))
{
    connect(m_chat, &ChatCompletionsRefiner::delta, this, &TranscriptRefiner::delta);
    connect(m_chat, &ChatCompletionsRefiner::completed, this, &TranscriptRefiner::completed);
    connect(m_chat, &ChatCompletionsRefiner::failed, this, &TranscriptRefiner::failed);
}

QString LocalRunnerRefiner::id() const
{
    return QStringLiteral("local");
}

QString LocalRunnerRefiner::label() const
{
    return QStringLiteral("Local model");
}

bool LocalRunnerRefiner::requiresRefresh(const RefinementSettings &) const
{
    return false;
}

void LocalRunnerRefiner::refresh(const RefinementSettings &)
{
}

RefinementPrepareResult LocalRunnerRefiner::prepare(const RefinementSettings &settings)
{
    // llama-server answers with whichever model it loaded.
    if (settings.localRunner.model.isEmpty() && settings.localRunner.runner != QStringLiteral("llama-server")) {
        return {false, QStringLiteral("Choose a cleanup model for %1.").arg(runnerName(settings.localRunner.runner))};
    }
    return {true, {}};
}

void LocalRunnerRefiner::refine(const QString &rawTranscript,
                                const QStringList &vocabulary,
                                const RefinementContext &context,
                                const RefinementSettings &settings)
{
    const RefinementPrepareResult prepared = prepare(settings);
    if (!prepared.ok) {
        emit failed(prepared.message);
        return;
    }
    const LocalRunnerSettings &runner = settings.localRunner;
    if (runner.runner == QStringLiteral("ollama")) {
        keepOllamaModelLoaded(runner.model);
    }
    m_chat->refine(rawTranscript, vocabulary, settings.bindingVocabulary, {},
                   localRunnerOrigin(runner.runner) + QStringLiteral("/v1"), runner.model,
                   settings.style, context);
}

// Ollama unloads an idle model after five minutes, and reloading it costs
// seconds. Its Chat Completions API takes no keep_alive, but a keep_alive a
// request sets stays on the loaded model, so a model-load request (empty
// prompt) beside the cleanup keeps it resident whichever finishes first.
void LocalRunnerRefiner::keepOllamaModelLoaded(const QString &model)
{
    QNetworkRequest request = endpointRequest(QUrl(localRunnerOrigin(QStringLiteral("ollama")) + QStringLiteral("/api/generate")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QJsonObject body{{QStringLiteral("model"), model}, {QStringLiteral("keep_alive"), -1}};
    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
}

void LocalRunnerRefiner::cancel()
{
    m_chat->cancel();
}

OllamaPull::OllamaPull(QObject *parent)
    : QObject(parent)
{
}

bool OllamaPull::isRunning() const
{
    return m_reply;
}

void OllamaPull::start(const QString &model, const QString &origin)
{
    cancel();
    m_buffer.clear();
    m_layers.clear();
    m_succeeded = false;
    m_error.clear();
    QNetworkRequest request = endpointRequest(QUrl(origin + QStringLiteral("/api/pull")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QJsonObject body{{QStringLiteral("model"), model}, {QStringLiteral("stream"), true}};
    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_reply = reply;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        if (reply == m_reply) readLines();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply != m_reply) return;
        m_buffer += reply->readAll() + '\n';
        readLines();
        m_reply.clear();
        if (m_succeeded) {
            emit finished();
        } else if (!m_error.isEmpty()) {
            emit failed(m_error);
        } else {
            emit failed(reply->error() == QNetworkReply::NoError
                            ? QStringLiteral("Ollama stopped before the download finished.")
                            : QStringLiteral("Could not download through Ollama: %1").arg(reply->errorString()));
        }
    });
}

// One JSON object per line: {"status", "digest", "total", "completed"} while
// layers download, {"status":"success"} at the end, {"error"} on failure.
void OllamaPull::readLines()
{
    if (m_reply) m_buffer += m_reply->readAll();
    int newline;
    while ((newline = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(newline).trimmed();
        m_buffer.remove(0, newline + 1);
        if (line.isEmpty()) continue;
        const QJsonObject event = QJsonDocument::fromJson(line).object();
        const QString error = event.value(QStringLiteral("error")).toString();
        if (!error.isEmpty()) {
            m_error = QStringLiteral("Ollama could not download the model: %1").arg(error);
            continue;
        }
        const QString status = event.value(QStringLiteral("status")).toString();
        m_succeeded = m_succeeded || status == QStringLiteral("success");
        const QString digest = event.value(QStringLiteral("digest")).toString();
        if (!digest.isEmpty()) {
            m_layers.insert(digest, {qint64(event.value(QStringLiteral("completed")).toDouble()),
                                     qint64(event.value(QStringLiteral("total")).toDouble())});
        }
        qint64 completed = 0;
        qint64 total = 0;
        for (const auto &layer : std::as_const(m_layers)) {
            completed += layer.first;
            total += layer.second;
        }
        emit progress(completed, total, status);
    }
}

void OllamaPull::cancel()
{
    if (QNetworkReply *reply = m_reply) {
        m_reply.clear();
        reply->abort();
    }
}

} // namespace speecher
