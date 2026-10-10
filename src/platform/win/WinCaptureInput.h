#pragma once

#include "dictation/DictationPorts.h"

#include <QAudioFormat>
#include <QList>
#include <QMutex>

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
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

// The sample layout a WASAPI mix format describes, or an invalid format when
// it is one the converter cannot read.
QAudioFormat audioFormatForWave(const WAVEFORMATEX &wave);

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
    // The audio it still holds, read once capture stops, without what a read
    // does to keep capture going, such as moving to a new default output. A
    // failure of it is no longer worth reporting.
    virtual void drain(QList<QByteArray> *chunks) { read(chunks); }
    // Why it records without the system's echo cancellation, once open and
    // after each read, for a stream that asks for it.
    virtual QString echoCancellationWarning() const { return {}; }
};

// How a WinCaptureInput words its own errors, that its capture could not
// start or stopped, each with the reason Windows gave.
struct CaptureWording {
    QString (*startFailed)(const QString &reason);
    QString (*stopped)(const QString &reason);
};

// Runs a WinCaptureStream on a thread of its own, which polls it every
// kLoopbackPollMs, and delivers its audio on the input's thread. A stop keeps
// capturing for postRollMs, drains the stream and delivers all of it before
// it returns, as QtAudioInput's does.
class WinCaptureInput : public AudioInput {
public:
    using StreamFactory = std::function<std::unique_ptr<WinCaptureStream>()>;

    WinCaptureInput(CaptureWording wording, StreamFactory createStream, int postRollMs, QObject *parent = nullptr);
    ~WinCaptureInput() override;

    bool start(QString *error = nullptr) override;
    void stop() override;
    bool isActive() const override;
    QString echoCancellationWarning() const override;

private:
    // Runs on the capture thread, and reports through opened why the stream
    // could not open, or nothing.
    void capture(quint64 generation, std::promise<QString> opened);
    // Run on the capture thread.
    void queueAudio(const QList<QByteArray> &chunks);
    void setEchoCancellationWarning(const QString &warning);
    // Run on the input's thread.
    void deliverAudio();
    void endCapture();
    void fail(quint64 generation, const QString &message);

    CaptureWording m_wording;
    StreamFactory m_createStream;
    int m_postRollMs;
    std::thread m_thread;
    HANDLE m_stopEvent = nullptr;
    // Bumped by every stop, so a failure the capture thread queued before it
    // is dropped rather than reaching the next start, and a delivery a stop
    // interrupted ends there. Its audio needs no such guard, as a stop
    // delivers all of it.
    quint64 m_generation = 0;
    // Guards what the capture thread hands the input's thread.
    mutable QMutex m_mutex;
    QList<QByteArray> m_queued;
    QString m_echoCancellationWarning;
};

} // namespace speecher
