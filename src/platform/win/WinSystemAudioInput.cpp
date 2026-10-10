#include "platform/win/WinSystemAudioInput.h"

#include "platform/audio/LoopbackPcm.h"

#include <QElapsedTimer>
#include <QMetaObject>
#include <QScopeGuard>

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <iterator>

namespace speecher {
namespace {

using Microsoft::WRL::ComPtr;

// A second of it, so a poll every 100 ms never lets it overflow.
constexpr REFERENCE_TIME kBufferDuration = 10'000'000;
// Polled rather than event-driven, because loopback signals nothing while
// nothing plays, and the poll that finds no audio is what fills the silence.
constexpr DWORD kPollMs = 100;

QString captureError(HRESULT result)
{
    return QStringLiteral("Could not capture system audio: %1").arg(qt_error_string(int(result)));
}

// Signals an event when the default output changes. Windows calls it on a
// thread of its own.
class DefaultOutputWatcher final
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                          IMMNotificationClient> {
public:
    explicit DefaultOutputWatcher(HANDLE changed)
        : m_changed(changed)
    {
    }

    STDMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override
    {
        // Windows reports a change once for each role; capture opens the
        // console role's output.
        if (flow == eRender && role == eConsole) {
            SetEvent(m_changed);
        }
        return S_OK;
    }
    STDMETHODIMP OnDeviceAdded(LPCWSTR) override { return S_OK; }
    STDMETHODIMP OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    STDMETHODIMP OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
    STDMETHODIMP OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

private:
    HANDLE m_changed;
};

// The loopback capture itself, which lives on the capture thread.
class LoopbackCapture {
public:
    LoopbackCapture() = default;
    ~LoopbackCapture()
    {
        if (m_watcher) {
            m_enumerator->UnregisterEndpointNotificationCallback(m_watcher.Get());
        }
        closeOutput();
        if (m_defaultOutputChanged) {
            CloseHandle(m_defaultOutputChanged);
        }
    }
    Q_DISABLE_COPY_MOVE(LoopbackCapture)

    // Starts watching the default output, then capturing from it, so a change
    // while it opens is reported after it and reopens.
    QString open();
    HANDLE defaultOutputChanged() const { return m_defaultOutputChanged; }
    // Leaves no output open when it fails.
    QString openDefaultOutput();
    // The audio since the last read, or the silence since then when nothing
    // played.
    QString read(QByteArray *pcm);

private:
    void closeOutput();
    HRESULT readPackets(QByteArray *pcm);

    HANDLE m_defaultOutputChanged = nullptr;
    ComPtr<IMMDeviceEnumerator> m_enumerator;
    ComPtr<DefaultOutputWatcher> m_watcher;
    ComPtr<IAudioClient> m_client;
    ComPtr<IAudioCaptureClient> m_capture;
    int m_bytesPerFrame = 0;
    LoopbackPcm m_pcm;
    QElapsedTimer m_clock;
};

QString LoopbackCapture::open()
{
    m_clock.start();
    m_defaultOutputChanged = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_defaultOutputChanged) {
        return captureError(HRESULT_FROM_WIN32(GetLastError()));
    }
    HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator),
                                      nullptr,
                                      CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&m_enumerator));
    if (FAILED(result)) {
        return captureError(result);
    }
    m_watcher = Microsoft::WRL::Make<DefaultOutputWatcher>(m_defaultOutputChanged);
    result = m_enumerator->RegisterEndpointNotificationCallback(m_watcher.Get());
    if (FAILED(result)) {
        m_watcher.Reset();
        return QStringLiteral("Could not follow the default sound output: %1").arg(qt_error_string(int(result)));
    }
    return openDefaultOutput();
}

QString LoopbackCapture::openDefaultOutput()
{
    closeOutput();
    ComPtr<IMMDevice> device;
    HRESULT result = m_enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (result == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
        return QStringLiteral("There is no sound output to capture system audio from.");
    }
    if (FAILED(result)) {
        return captureError(result);
    }
    ComPtr<IAudioClient> client;
    result = device->Activate(__uuidof(IAudioClient),
                              CLSCTX_INPROC_SERVER,
                              nullptr,
                              reinterpret_cast<void **>(client.GetAddressOf()));
    if (FAILED(result)) {
        return captureError(result);
    }
    WAVEFORMATEX *mixFormat = nullptr;
    result = client->GetMixFormat(&mixFormat);
    if (FAILED(result)) {
        return captureError(result);
    }
    const auto freeMixFormat = qScopeGuard([mixFormat] { CoTaskMemFree(mixFormat); });
    const QAudioFormat format = audioFormatForWave(*mixFormat);
    if (!format.isValid()) {
        return QStringLiteral("Could not capture system audio: the sound output uses a format Speecher cannot read.");
    }
    ComPtr<IAudioCaptureClient> capture;
    if (FAILED(result = client->Initialize(
                   AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, kBufferDuration, 0, mixFormat, nullptr))
        || FAILED(result = client->GetService(IID_PPV_ARGS(&capture)))
        || FAILED(result = client->Start())) {
        return captureError(result);
    }
    m_client = client;
    m_capture = capture;
    m_bytesPerFrame = mixFormat->nBlockAlign;
    m_pcm.useFormat(format);
    return {};
}

QString LoopbackCapture::read(QByteArray *pcm)
{
    const HRESULT result = m_capture ? readPackets(pcm) : S_OK;
    if (result == AUDCLNT_E_DEVICE_INVALIDATED) {
        // Changing the output's format ends the stream on an output that stays
        // the default, so it reopens at once. A removed output leaves nothing
        // to open until Windows reports the new default, and the stream is
        // silent until then.
        openDefaultOutput();
    } else if (FAILED(result)) {
        return QStringLiteral("System audio capture stopped: %1").arg(qt_error_string(int(result)));
    }
    if (pcm->isEmpty()) {
        *pcm = m_pcm.silenceUntil(m_clock.elapsed());
    }
    return {};
}

void LoopbackCapture::closeOutput()
{
    if (m_client) {
        m_client->Stop();
    }
    m_capture.Reset();
    m_client.Reset();
}

HRESULT LoopbackCapture::readPackets(QByteArray *pcm)
{
    UINT32 packetFrames = 0;
    HRESULT result;
    while (SUCCEEDED(result = m_capture->GetNextPacketSize(&packetFrames)) && packetFrames > 0) {
        BYTE *data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;
        result = m_capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
        if (FAILED(result)) {
            return result;
        }
        const qsizetype size = qsizetype(frames) * m_bytesPerFrame;
        // A silent packet's data is to be ignored and read as silence.
        const QByteArray packet = flags & AUDCLNT_BUFFERFLAGS_SILENT
            ? QByteArray(size, '\0')
            : QByteArray(reinterpret_cast<const char *>(data), size);
        result = m_capture->ReleaseBuffer(frames);
        if (FAILED(result)) {
            return result;
        }
        *pcm += m_pcm.convert(packet);
    }
    return result;
}

} // namespace

QAudioFormat audioFormatForWave(const WAVEFORMATEX &wave)
{
    WORD tag = wave.wFormatTag;
    if (tag == WAVE_FORMAT_EXTENSIBLE && wave.cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        const GUID &subFormat = reinterpret_cast<const WAVEFORMATEXTENSIBLE &>(wave).SubFormat;
        tag = subFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT ? WAVE_FORMAT_IEEE_FLOAT
            : subFormat == KSDATAFORMAT_SUBTYPE_PCM        ? WAVE_FORMAT_PCM
                                                           : WAVE_FORMAT_UNKNOWN;
    }
    QAudioFormat::SampleFormat sampleFormat = QAudioFormat::Unknown;
    if (tag == WAVE_FORMAT_IEEE_FLOAT && wave.wBitsPerSample == 32) {
        sampleFormat = QAudioFormat::Float;
    } else if (tag == WAVE_FORMAT_PCM && wave.wBitsPerSample == 8) {
        sampleFormat = QAudioFormat::UInt8;
    } else if (tag == WAVE_FORMAT_PCM && wave.wBitsPerSample == 16) {
        sampleFormat = QAudioFormat::Int16;
    } else if (tag == WAVE_FORMAT_PCM && wave.wBitsPerSample == 32) {
        sampleFormat = QAudioFormat::Int32;
    }
    QAudioFormat format;
    format.setSampleRate(int(wave.nSamplesPerSec));
    format.setChannelCount(wave.nChannels);
    format.setSampleFormat(sampleFormat);
    // Padded frames would be misread as samples.
    if (format.bytesPerFrame() != wave.nBlockAlign) {
        format.setSampleFormat(QAudioFormat::Unknown);
    }
    return format;
}

WinSystemAudioInput::WinSystemAudioInput(QObject *parent)
    : AudioInput(parent)
{
}

WinSystemAudioInput::~WinSystemAudioInput()
{
    stop();
}

bool WinSystemAudioInput::start(QString *error)
{
    if (m_thread.joinable()) {
        return true;
    }
    QString message;
    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (m_stopEvent) {
        std::promise<QString> started;
        std::future<QString> opened = started.get_future();
        m_thread = std::thread(&WinSystemAudioInput::capture, this, m_generation, std::move(started));
        message = opened.get();
    } else {
        message = captureError(HRESULT_FROM_WIN32(GetLastError()));
    }
    if (message.isEmpty()) {
        return true;
    }
    stop();
    if (error) {
        *error = message;
    }
    return false;
}

void WinSystemAudioInput::stop()
{
    if (m_thread.joinable()) {
        SetEvent(m_stopEvent);
        m_thread.join();
    }
    if (m_stopEvent) {
        CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
        ++m_generation;
    }
}

bool WinSystemAudioInput::isActive() const
{
    return m_thread.joinable();
}

void WinSystemAudioInput::capture(quint64 generation, std::promise<QString> started)
{
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const auto uninitialize = qScopeGuard([apartment] {
        if (SUCCEEDED(apartment)) {
            CoUninitialize();
        }
    });
    LoopbackCapture loopback;
    if (const QString error = loopback.open(); !error.isEmpty()) {
        started.set_value(error);
        return;
    }
    started.set_value({});

    const HANDLE events[] = {m_stopEvent, loopback.defaultOutputChanged()};
    for (;;) {
        const DWORD woke = WaitForMultipleObjects(DWORD(std::size(events)), events, FALSE, kPollMs);
        if (woke == WAIT_OBJECT_0) {
            return;
        }
        QByteArray pcm;
        QString error;
        if (woke == WAIT_FAILED) {
            error = QStringLiteral("System audio capture stopped: %1").arg(qt_error_string(int(GetLastError())));
        } else if (woke == WAIT_OBJECT_0 + 1) {
            error = loopback.openDefaultOutput();
        }
        if (error.isEmpty()) {
            error = loopback.read(&pcm);
        }
        if (!pcm.isEmpty()) {
            QMetaObject::invokeMethod(
                this,
                [this, pcm, generation] {
                    if (generation == m_generation) {
                        emit audioChunk(pcm);
                    }
                },
                Qt::QueuedConnection);
        }
        if (!error.isEmpty()) {
            QMetaObject::invokeMethod(this, [this, error, generation] { fail(generation, error); }, Qt::QueuedConnection);
            return;
        }
    }
}

void WinSystemAudioInput::fail(quint64 generation, const QString &message)
{
    if (generation != m_generation) {
        return;
    }
    stop();
    emit failed(message);
}

} // namespace speecher
