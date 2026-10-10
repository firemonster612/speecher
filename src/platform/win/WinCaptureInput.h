#pragma once

#include "dictation/DictationPorts.h"

#include <QList>
#include <QMutex>

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/implements.h>

#include <functional>
#include <future>
#include <memory>
#include <thread>

namespace speecher {

// A second of it, so a poll every kLoopbackPollMs never lets a capture's
// buffer overflow.
inline constexpr REFERENCE_TIME kCaptureBufferDuration = 10'000'000;

enum class AudioEndpoint { Microphone, SoundOutput };

// Why the audio engine refused a capture from endpoint; Windows has no text
// for its own errors.
QString audioErrorText(HRESULT result, AudioEndpoint endpoint);

// Signals an event when the default output changes. Windows calls it on a
// thread of its own, so the watcher owns the event and keeps it for as long
// as Windows holds a reference to it.
class DefaultOutputWatcher final
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                          IMMNotificationClient> {
public:
    DefaultOutputWatcher();
    ~DefaultOutputWatcher() override;

    // Null when the event could not be made.
    HANDLE changed() const { return m_changed; }
    // Whether the default output changed since the last take.
    bool takeChange();

    STDMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override;
    STDMETHODIMP OnDeviceAdded(LPCWSTR) override { return S_OK; }
    STDMETHODIMP OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    STDMETHODIMP OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
    STDMETHODIMP OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

private:
    HANDLE m_changed;
};

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
// kLoopbackPollMs, and delivers its audio on the input's thread. name, such
// as "Microphone", begins the input's own errors. A stop keeps capturing for
// postRollMs, reads once more and delivers all of it before it returns, as
// QtAudioInput's does.
class WinCaptureInput : public AudioInput {
public:
    using StreamFactory = std::function<std::unique_ptr<WinCaptureStream>()>;

    WinCaptureInput(const QString &name, StreamFactory createStream, int postRollMs, QObject *parent = nullptr);
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
    // Runs on the capture thread.
    void queueAudio(const QList<QByteArray> &chunks);
    // Run on the input's thread.
    void deliverAudio();
    void endCapture();
    void fail(quint64 generation, const QString &message);

    QString m_name;
    StreamFactory m_createStream;
    int m_postRollMs;
    std::thread m_thread;
    HANDLE m_stopEvent = nullptr;
    // Bumped by every stop, so a failure the capture thread queued before it
    // is dropped rather than reaching the next start. Its audio needs no
    // such guard, as a stop delivers all of it.
    quint64 m_generation = 0;
    QMutex m_queuedMutex;
    QList<QByteArray> m_queued;
    QString m_echoCancellationWarning;
};

} // namespace speecher
