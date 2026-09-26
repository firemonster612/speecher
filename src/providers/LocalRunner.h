#pragma once

#include "core/LocalModelCatalog.h"
#include "dictation/DictationPorts.h"

#include <QNetworkAccessManager>
#include <QPointer>
#include <QStringList>

#include <optional>

class QNetworkReply;

namespace speecher {

class ChatCompletionsRefiner;

// A Local Runner found answering on this computer.
struct DetectedRunner {
    // "ollama", "lmstudio" or "llama-server", as LocalRunnerSettings stores it.
    QString id;
    QString name;
    // Empty where the runner does not report one (LM Studio).
    QString version;
    // Where its Chat Completions API lives, e.g. http://127.0.0.1:11434/v1.
    QString apiBase;
    // Chat models it can serve; for llama-server the one it loaded.
    QStringList models;
};

// The origin a runner listens on by default, e.g. http://127.0.0.1:11434.
QString localRunnerOrigin(const QString &runnerId);

// Each probe asks for a server-specific answer, since KoboldCpp and Lemonade
// also serve Ollama's /api/version and /api/tags. Blocking, up to about
// three requests of timeoutMs each; run it through runProviderProbe.
std::optional<DetectedRunner> probeOllama(const QString &origin, int timeoutMs);
std::optional<DetectedRunner> probeLmStudio(const QString &origin, int timeoutMs);
std::optional<DetectedRunner> probeLlamaServer(const QString &origin, int timeoutMs);
// Every runner on its default port, Ollama first.
QList<DetectedRunner> detectLocalRunners(int timeoutMs = 1500);

// A cleanup model Speecher suggests pulling through Ollama.
struct CleanupModel {
    QString ollamaTag;
    QString name;
    qint64 sizeBytes = 0;
};

// The machine classes notes-llm.md's latency table has rows for.
enum class CleanupHardware {
    Cpu,
    IntegratedGpu,
    DedicatedGpu,
    AppleBase,
    ApplePro,
    AppleMax,
};

// The latency table's row for this machine. Apple chips are told apart by
// name, and a graphics card too small for the larger model counts as the
// processor, which is where that model would end up running.
CleanupHardware cleanupHardwareFor(const HardwareProfile &hardware);

// The larger model when it cleans a dictation in 2 s or less, the smaller
// when it takes 3 s or less, else nothing: cloud refinement or none will
// feel faster.
std::optional<CleanupModel> suggestedCleanupModel(CleanupHardware hardware);

// Refinement through a Local Runner's Chat Completions API.
class LocalRunnerRefiner final : public TranscriptRefiner {
    Q_OBJECT

public:
    explicit LocalRunnerRefiner(QObject *parent = nullptr);

    QString id() const override;
    QString label() const override;
    bool requiresRefresh(const RefinementSettings &settings) const override;
    void refresh(const RefinementSettings &settings) override;
    RefinementPrepareResult prepare(const RefinementSettings &settings) override;
    void refine(const QString &rawTranscript,
                const QStringList &vocabulary,
                const RefinementContext &context,
                const RefinementSettings &settings) override;
    void cancel() override;

private:
    void keepOllamaModelLoaded(const QString &model);

    ChatCompletionsRefiner *m_chat = nullptr;
    QNetworkAccessManager m_network;
};

// Downloads a model into Ollama through POST /api/pull.
class OllamaPull final : public QObject {
    Q_OBJECT

public:
    explicit OllamaPull(QObject *parent = nullptr);

    void start(const QString &model, const QString &origin = localRunnerOrigin(QStringLiteral("ollama")));
    void cancel();
    bool isRunning() const;

signals:
    // Bytes across every layer seen so far, and Ollama's status line.
    void progress(qint64 completedBytes, qint64 totalBytes, const QString &status);
    void finished();
    void failed(const QString &message);

private:
    void readLines();

    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QByteArray m_buffer;
    QHash<QString, QPair<qint64, qint64>> m_layers;
    bool m_succeeded = false;
    QString m_error;
};

} // namespace speecher
