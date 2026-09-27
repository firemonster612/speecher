#pragma once

#include "core/AppSettings.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <functional>
#include <optional>

struct transcribe_model;
struct transcribe_session;

namespace speecher {

// Resolve a raw final hypothesis against the stream's committed text.
QString finalStreamText(const QString &committed, const QString &rawFinal);

// transcribe.cpp behind Speecher's own types: its pre-1.0 ABI stops here.
// One thread drives an engine at a time; every call blocks.
class LocalSpeechEngine {
public:
    struct Device {
        enum class Type {
            Cpu,
            Gpu,
            IntegratedGpu,
            Accelerator,
        };
        // Stable hardware id (the PCI bus id), empty when the backend has none.
        QString id;
        // What a person would call it, e.g. "Apple M4 Max".
        QString description;
        // cpu, metal, vulkan, cuda, ...
        QString kind;
        Type type = Type::Cpu;
        quint64 memoryTotalBytes = 0;
        quint64 memoryFreeBytes = 0;
    };

    // The devices inference can run on. The first call loads the backends.
    static QList<Device> devices();

    struct StreamText {
        // Append-only while the stream runs.
        QString committed;
        QString tentative;
    };

    // Polled during inference; returning true makes the running call stop.
    explicit LocalSpeechEngine(std::function<bool()> shouldAbort);
    ~LocalSpeechEngine();
    LocalSpeechEngine(const LocalSpeechEngine &) = delete;
    LocalSpeechEngine &operator=(const LocalSpeechEngine &) = delete;

    // An explicit backend or card that cannot take the model fails the load
    // rather than falling back to another.
    bool load(const QString &modelPath, const LocalRunsOn &runsOn, QString *error);
    void unload();
    bool isLoaded(const QString &modelPath, const LocalRunsOn &runsOn) const;
    // Where the loaded model runs, e.g. "NVIDIA GeForce RTX 3060 (CUDA)";
    // empty while none is loaded.
    QString runsOnDescription() const;
    bool streams() const;

    // Audio is 16 kHz mono signed 16-bit PCM. A call that fails without
    // setting error was aborted. prompt biases decoding toward its words on a
    // model that accepts an initial prompt; other models ignore it.
    std::optional<QString> transcribe(const QByteArray &pcm16, const QString &prompt, QString *error);

    bool beginStream(QString *error);
    bool feed(const QByteArray &pcm16, StreamText *text, QString *error);
    // The whole final transcript, preserving committed text if the raw
    // hypothesis shrank below it.
    std::optional<QString> finalize(QString *error);

    // The Speed Test: seconds the loaded model takes for 10 s of speech,
    // measured on a bundled clip after one untimed run of it.
    std::optional<double> speedTestSeconds(QString *error);

private:
    static bool abortRequested(void *engine);

    std::function<bool()> m_shouldAbort;
    transcribe_model *m_model = nullptr;
    transcribe_session *m_session = nullptr;
    QString m_modelPath;
    LocalRunsOn m_runsOn;
    bool m_streams = false;
};

} // namespace speecher
