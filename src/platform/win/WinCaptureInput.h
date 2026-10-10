#pragma once

#include "dictation/DictationPorts.h"

#include <QList>

#include <windows.h>

#include <functional>
#include <future>
#include <memory>
#include <thread>

namespace speecher {

// A WASAPI capture, which lives on the capture thread from its open to its
// destruction.
class WinCaptureStream {
public:
    virtual ~WinCaptureStream() = default;
    // Why it could not open, or nothing.
    virtual QString open() = 0;
    // The audio since the last read, as 16 kHz mono s16, and why capture
    // stopped, or nothing while it runs.
    virtual QString read(QList<QByteArray> *chunks) = 0;
    // Why it records without the system's echo cancellation, once open, for
    // a stream that asks for it.
    virtual QString echoCancellationWarning() const { return {}; }
};

// Runs a WinCaptureStream on a thread of its own, which polls it every
// kLoopbackPollMs, and delivers its audio on the input's thread.
class WinCaptureInput : public AudioInput {
public:
    using StreamFactory = std::function<std::unique_ptr<WinCaptureStream>()>;

    explicit WinCaptureInput(StreamFactory createStream, QObject *parent = nullptr);
    ~WinCaptureInput() override;

    bool start(QString *error = nullptr) override;
    void stop() override;
    bool isActive() const override;
    QString echoCancellationWarning() const override;

private:
    struct Opened {
        QString error;
        QString echoCancellationWarning;
    };

    // Runs on the capture thread, and reports through opened whether the
    // stream opened.
    void capture(quint64 generation, std::promise<Opened> opened);
    // Run on the input's thread.
    void fail(quint64 generation, const QString &message);

    StreamFactory m_createStream;
    std::thread m_thread;
    HANDLE m_stopEvent = nullptr;
    // Bumped by every stop, so audio and failures the capture thread queued
    // before it are dropped rather than reaching the next start.
    quint64 m_generation = 0;
    QString m_echoCancellationWarning;
};

} // namespace speecher
