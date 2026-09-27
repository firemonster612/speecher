#include "providers/LocalSpeechEngine.h"
#include "core/settings/SettingsSchema.h"

#include <QElapsedTimer>
#include <QFile>
#include <QtEndian>

#include <transcribe.h>

#include <array>
#include <mutex>
#include <vector>

namespace speecher {
namespace {

// Speecher transcribes English. The multilingual models (Cohere, Qwen3-ASR,
// Whisper) would otherwise guess the language, and Cohere needs the hint.
constexpr auto language = "en";
constexpr double speedTestReferenceSeconds = 10.0;
constexpr double sampleRateHz = 16000.0;

QString statusMessage(transcribe_status status)
{
    switch (status) {
    case TRANSCRIBE_ERR_FILE_NOT_FOUND:
        return QStringLiteral("The model file is missing. Download it again on the %1 page.").arg(paneTitle(QStringLiteral("localModels")));
    case TRANSCRIBE_ERR_GGUF:
    case TRANSCRIBE_ERR_UNSUPPORTED_ARCH:
    case TRANSCRIBE_ERR_UNSUPPORTED_VARIANT:
        return QStringLiteral("The model file is damaged or not a speech model. Delete it and download it again on the %1 page.")
            .arg(paneTitle(QStringLiteral("localModels")));
    case TRANSCRIBE_ERR_OOM:
        return QStringLiteral("This computer ran out of memory for the model. Close other programs or choose a smaller model.");
    case TRANSCRIBE_ERR_INPUT_TOO_LONG:
        return QStringLiteral("The recording is longer than this model can transcribe at once.");
    default:
        return QStringLiteral("The local model failed: %1.")
            .arg(QString::fromUtf8(transcribe_status_string(status)));
    }
}

bool succeeded(transcribe_status status, QString *error)
{
    if (status == TRANSCRIBE_OK) {
        return true;
    }
    *error = statusMessage(status);
    return false;
}

// Linux and Windows builds carry the ggml backends as modules in
// libtranscribe's own directory, which is where this looks; static builds have
// them compiled in and this is a no-op.
void initBackendsOnce()
{
    static std::once_flag once;
    std::call_once(once, [] { transcribe_init_backends_default(); });
}

LocalSpeechEngine::Device::Type deviceType(transcribe_device_type type)
{
    switch (type) {
    case TRANSCRIBE_DEVICE_TYPE_GPU:
        return LocalSpeechEngine::Device::Type::Gpu;
    case TRANSCRIBE_DEVICE_TYPE_IGPU:
        return LocalSpeechEngine::Device::Type::IntegratedGpu;
    case TRANSCRIBE_DEVICE_TYPE_ACCEL:
        return LocalSpeechEngine::Device::Type::Accelerator;
    case TRANSCRIBE_DEVICE_TYPE_CPU:
        break;
    }
    return LocalSpeechEngine::Device::Type::Cpu;
}

// In localBackends order.
constexpr std::array<transcribe_backend_request, localBackends.size()> backendRequests{
    TRANSCRIBE_BACKEND_AUTO,   TRANSCRIBE_BACKEND_CPU,  TRANSCRIBE_BACKEND_METAL,
    TRANSCRIBE_BACKEND_VULKAN, TRANSCRIBE_BACKEND_CUDA, TRANSCRIBE_BACKEND_ROCM,
};

std::optional<transcribe_backend_request> backendRequest(const QString &backend)
{
    for (size_t index = 0; index < localBackends.size(); ++index) {
        if (backend == QLatin1String(localBackends[index].kind)) return backendRequests[index];
    }
    return std::nullopt;
}

// The registered device with that PCI bus id, of that backend unless it is
// "auto".
transcribe_device_t findDevice(const LocalRunsOn &runsOn)
{
    const QByteArray id = runsOn.deviceId.toUtf8();
    const QByteArray kind = runsOn.backend.toUtf8();
    const bool anyKind = runsOn.backend == QStringLiteral("auto");
    for (int index = 0; index < transcribe_device_count(); ++index) {
        transcribe_device_t device = transcribe_device_get(index);
        transcribe_device_info info;
        transcribe_device_info_init(&info);
        if (device && transcribe_device_get_info(device, &info) == TRANSCRIBE_OK
            && info.device_id && id == info.device_id && info.kind && (anyKind || kind == info.kind)) {
            return device;
        }
    }
    return nullptr;
}

std::vector<float> floatPcm(const QByteArray &pcm16)
{
    std::vector<float> samples(size_t(pcm16.size() / 2));
    for (size_t index = 0; index < samples.size(); ++index) {
        samples[index] = float(qFromLittleEndian<qint16>(pcm16.constData() + 2 * index)) / 32768.0f;
    }
    return samples;
}

} // namespace

QString finalStreamText(const QString &committed, const QString &rawFinal)
{
    // Moonshine may shrink its raw hypothesis even after committing words.
    // Replacing the attempt with that snapshot would discard those words.
    const QString committedText = committed.trimmed();
    const QString finalText = rawFinal.trimmed();
    return finalText.size() < committedText.size() ? committedText : finalText;
}

QList<LocalSpeechEngine::Device> LocalSpeechEngine::devices()
{
    initBackendsOnce();
    QList<Device> result;
    for (int index = 0; index < transcribe_device_count(); ++index) {
        transcribe_device_info info;
        transcribe_device_info_init(&info);
        transcribe_device_t device = transcribe_device_get(index);
        if (!device || transcribe_device_get_info(device, &info) != TRANSCRIBE_OK) {
            continue;
        }
        result.append({QString::fromUtf8(info.device_id),
                       QString::fromUtf8(info.description),
                       QString::fromUtf8(info.kind),
                       deviceType(info.device_type),
                       info.memory_total,
                       info.memory_free});
    }
    return result;
}

LocalSpeechEngine::LocalSpeechEngine(std::function<bool()> shouldAbort)
    : m_shouldAbort(std::move(shouldAbort))
{
}

LocalSpeechEngine::~LocalSpeechEngine()
{
    unload();
}

bool LocalSpeechEngine::abortRequested(void *engine)
{
    return static_cast<LocalSpeechEngine *>(engine)->m_shouldAbort();
}

bool LocalSpeechEngine::load(const QString &modelPath, const LocalRunsOn &runsOn, QString *error)
{
    unload();
    initBackendsOnce();
    const QString backendName = localBackendName(runsOn.backend);
    const std::optional<transcribe_backend_request> backend = backendRequest(runsOn.backend);
    if (!backend || !transcribe_backend_available(*backend)) {
        *error = QStringLiteral("%1 is not available on this computer. Choose where the model runs "
                                "on the Local models page.")
                     .arg(backendName);
        return false;
    }
    transcribe_model_load_params params;
    transcribe_model_load_params_init(&params);
    params.backend = *backend;
    if (!runsOn.deviceId.isEmpty()) {
        params.device = findDevice(runsOn);
        if (!params.device) {
            *error = *backend == TRANSCRIBE_BACKEND_AUTO
                ? QStringLiteral("The chosen graphics card is missing. Choose where the model runs "
                                 "on the Local models page.")
                : QStringLiteral("The graphics card chosen for %1 is missing. Choose where the "
                                 "model runs on the Local models page.")
                      .arg(backendName);
            return false;
        }
    }
    const QByteArray path = modelPath.toUtf8();
    const transcribe_status status = transcribe_model_load_file(path.constData(), &params, &m_model);
    if (status == TRANSCRIBE_ERR_BACKEND && *backend != TRANSCRIBE_BACKEND_AUTO) {
        *error = QStringLiteral("%1 could not load the model. Choose where the model runs on the "
                                "Local models page.")
                     .arg(backendName);
        return false;
    }
    if (!succeeded(status, error)) {
        return false;
    }
    if (!succeeded(transcribe_session_init(m_model, nullptr, &m_session), error)) {
        unload();
        return false;
    }
    transcribe_set_abort_callback(m_session, &LocalSpeechEngine::abortRequested, this);
    transcribe_capabilities capabilities;
    transcribe_capabilities_init(&capabilities);
    m_streams = transcribe_model_get_capabilities(m_model, &capabilities) == TRANSCRIBE_OK
        && capabilities.supports_streaming;
    m_modelPath = modelPath;
    m_runsOn = runsOn;
    return true;
}

void LocalSpeechEngine::unload()
{
    transcribe_session_free(m_session);
    transcribe_model_free(m_model);
    m_session = nullptr;
    m_model = nullptr;
    m_modelPath.clear();
    m_runsOn = {};
    m_streams = false;
}

bool LocalSpeechEngine::isLoaded(const QString &modelPath, const LocalRunsOn &runsOn) const
{
    return m_session && m_modelPath == modelPath && m_runsOn == runsOn;
}

QString LocalSpeechEngine::runsOnDescription() const
{
    transcribe_device_info info;
    transcribe_device_info_init(&info);
    const transcribe_device_t device = transcribe_model_device(m_model);
    if (!device || transcribe_device_get_info(device, &info) != TRANSCRIBE_OK) {
        return {};
    }
    return QStringLiteral("%1 (%2)").arg(QString::fromUtf8(info.description).simplified(),
                                         localBackendName(QString::fromUtf8(info.kind)));
}

bool LocalSpeechEngine::streams() const
{
    return m_streams;
}

std::optional<QString> LocalSpeechEngine::transcribe(const QByteArray &pcm16, QString *error)
{
    const std::vector<float> pcm = floatPcm(pcm16);
    if (pcm.empty()) {
        return QString();
    }
    // A cancelled attempt can leave a stream active, which blocks a run.
    transcribe_stream_reset(m_session);
    transcribe_run_params params;
    transcribe_run_params_init(&params);
    params.timestamps = TRANSCRIBE_TIMESTAMPS_NONE;
    params.language = language;
    const transcribe_status status = transcribe_run(m_session, pcm.data(), int(pcm.size()), &params);
    if (status == TRANSCRIBE_ERR_ABORTED) {
        return std::nullopt;
    }
    if (!succeeded(status, error)) {
        return std::nullopt;
    }
    return QString::fromUtf8(transcribe_full_text(m_session)).trimmed();
}

bool LocalSpeechEngine::beginStream(QString *error)
{
    // A cancelled attempt can leave the previous stream active.
    transcribe_stream_reset(m_session);
    transcribe_run_params params;
    transcribe_run_params_init(&params);
    params.timestamps = TRANSCRIBE_TIMESTAMPS_NONE;
    params.language = language;
    return succeeded(transcribe_stream_begin(m_session, &params, nullptr), error);
}

bool LocalSpeechEngine::feed(const QByteArray &pcm16, StreamText *text, QString *error)
{
    const std::vector<float> pcm = floatPcm(pcm16);
    if (pcm.empty()) {
        return true;
    }
    const transcribe_status status =
        transcribe_stream_feed(m_session, pcm.data(), int(pcm.size()), nullptr);
    if (status == TRANSCRIBE_ERR_ABORTED || !succeeded(status, error)) {
        return false;
    }
    transcribe_stream_text snapshot;
    transcribe_stream_text_init(&snapshot);
    if (!succeeded(transcribe_stream_get_text(m_session, &snapshot), error)) {
        return false;
    }
    text->committed = QString::fromUtf8(snapshot.committed_text, qsizetype(snapshot.committed_text_bytes));
    text->tentative = QString::fromUtf8(snapshot.tentative_text, qsizetype(snapshot.tentative_text_bytes));
    return true;
}

std::optional<QString> LocalSpeechEngine::finalize(QString *error)
{
    const transcribe_status status = transcribe_stream_finalize(m_session, nullptr);
    if (status == TRANSCRIBE_ERR_ABORTED || !succeeded(status, error)) {
        return std::nullopt;
    }
    transcribe_stream_text snapshot;
    transcribe_stream_text_init(&snapshot);
    if (!succeeded(transcribe_stream_get_text(m_session, &snapshot), error)) {
        return std::nullopt;
    }
    return finalStreamText(
        QString::fromUtf8(snapshot.committed_text, qsizetype(snapshot.committed_text_bytes)),
        QString::fromUtf8(snapshot.full_text, qsizetype(snapshot.full_text_bytes)));
}

std::optional<double> LocalSpeechEngine::speedTestSeconds(QString *error)
{
    QFile clip(QStringLiteral(":/speedtest/librispeech-6930-75918-0018.s16le"));
    if (!clip.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("The speed test clip is missing from this build.");
        return std::nullopt;
    }
    const QByteArray pcm16 = clip.readAll();
    // Untimed: a backend's first run pays one-off costs, such as Vulkan
    // compiling its shaders, that dictation after it never sees.
    if (!transcribe(pcm16, error)) {
        return std::nullopt;
    }
    QElapsedTimer timer;
    timer.start();
    if (!transcribe(pcm16, error)) {
        return std::nullopt;
    }
    const double clipSeconds = double(pcm16.size() / 2) / sampleRateHz;
    return double(timer.elapsed()) / 1000.0 * speedTestReferenceSeconds / clipSeconds;
}

} // namespace speecher
