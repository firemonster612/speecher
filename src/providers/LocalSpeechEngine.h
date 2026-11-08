#pragma once

#include "core/AppSettings.h"
#include "providers/SpeechTranscriber.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

namespace speecher {

// Resolve a raw final hypothesis against the stream's committed text.
QString finalStreamText(const QString &committed, const QString &rawFinal);

// A Local Model loaded once, with a session of its own for each use of it.
// create() is transcribe.cpp behind Speecher's own types: its pre-1.0 ABI
// stops here. One thread drives an engine and its sessions; every call blocks.
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

    // One stream or run's state on the loaded model, which several sessions
    // share. A session goes before the model it was opened on unloads.
    class Session {
    public:
        virtual ~Session() = default;
        // Audio is 16 kHz mono signed 16-bit PCM. spokenLanguage is a
        // SpeechSettings::language: the model's hint, or none for Automatic,
        // which the model detects. A call that fails without setting error
        // was aborted.
        virtual std::optional<QString> transcribe(const QByteArray &pcm16, const QString &spokenLanguage,
                                                  QString *error) = 0;
        // The same, and when each part of the text was spoken. segments stays
        // empty unless the model times whole segments: Whisper-style models
        // do, while one reporting word or token timings gives a single
        // segment for the whole run.
        virtual std::optional<QString> transcribeTimed(const QByteArray &pcm16, const QString &spokenLanguage,
                                                       QList<TranscriptSegment> *segments, QString *error) = 0;
        // Ends any stream before.
        virtual bool beginStream(const QString &spokenLanguage, QString *error) = 0;
        virtual bool feed(const QByteArray &pcm16, StreamText *text, QString *error) = 0;
        // The whole final transcript, preserving committed text if the raw
        // hypothesis shrank below it.
        virtual std::optional<QString> finalize(QString *error) = 0;
    };

    // transcribe.cpp's.
    static std::unique_ptr<LocalSpeechEngine> create();
    virtual ~LocalSpeechEngine() = default;

    // An explicit backend or card that cannot take the model fails the load
    // rather than falling back to another. No session may be open.
    virtual bool load(const QString &modelPath, const LocalRunsOn &runsOn, QString *error) = 0;
    // No session may be open.
    virtual void unload() = 0;
    virtual bool isLoaded(const QString &modelPath, const LocalRunsOn &runsOn) const = 0;
    // Where the loaded model runs, e.g. "NVIDIA GeForce RTX 3060 (CUDA)";
    // empty while none is loaded.
    virtual QString runsOnDescription() const = 0;
    virtual bool streams() const = 0;
    // A session on the loaded model; shouldAbort is polled during its calls,
    // and returning true makes the running one stop. Null with error set when
    // it can't open.
    virtual std::unique_ptr<Session> openSession(std::function<bool()> shouldAbort, QString *error) = 0;
};

// The Speed Test: seconds the session's model takes for 10 s of speech,
// measured on a bundled clip after one untimed run of it.
std::optional<double> speedTestSeconds(LocalSpeechEngine::Session &session, QString *error);

} // namespace speecher
