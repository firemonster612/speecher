#include "platform/win/WinSystemAudioInput.h"

#include "platform/audio/LoopbackPcm.h"
#include "platform/audio/LoopbackReopen.h"

#include <QScopeGuard>

#include <wrl/client.h>

namespace speecher {
namespace {

using Microsoft::WRL::ComPtr;

// The unit of GetBuffer's performance counter positions and of interrupt time.
constexpr UINT64 kHundredNsPerMs = 10'000;

QString captureError(HRESULT result)
{
    return QStringLiteral("Could not capture system audio: %1")
        .arg(audioErrorText(result, AudioEndpoint::SoundOutput));
}

// Two clocks: the timeline, and with it the reopen allowance, runs on
// unbiased interrupt time, which stops while the machine sleeps, so a gap on
// it is capture stalling and is filled, while sleep is skipped. GetBuffer
// places packets on the performance counter, which counts sleep, so a packet
// is placed by comparing its position with that counter now.
qint64 unbiasedInterruptTimeMs()
{
    ULONGLONG time = 0;
    QueryUnbiasedInterruptTime(&time);
    return qint64(time / kHundredNsPerMs);
}

qint64 performanceCounterMs()
{
    LARGE_INTEGER count;
    LARGE_INTEGER frequency;
    QueryPerformanceCounter(&count);
    QueryPerformanceFrequency(&frequency);
    // Split so the multiplication cannot overflow.
    return count.QuadPart / frequency.QuadPart * 1000 + count.QuadPart % frequency.QuadPart * 1000 / frequency.QuadPart;
}

class LoopbackCapture final : public WinCaptureStream {
public:
    LoopbackCapture() = default;
    ~LoopbackCapture() override
    {
        if (m_watcher) {
            m_enumerator->UnregisterEndpointNotificationCallback(m_watcher.Get());
        }
        closeOutput();
    }
    Q_DISABLE_COPY_MOVE(LoopbackCapture)

    // Starts watching the default output, then capturing from it, so a change
    // while it opens is reported after it and reopens.
    QString open() override;
    // The audio since the last read, with the silence around it. An output
    // that closes, or stops being the default, is reopened on this and later
    // reads until the reopen allowance runs out, which fails the read.
    QString read(QList<QByteArray> *chunks) override;
    // The open output's last audio, with the silence before the stop.
    void drain(QList<QByteArray> *chunks) override;

private:
    // Expects no output open, and leaves none open when it fails.
    HRESULT openDefaultOutput();
    QString reopenDefaultOutput(qint64 elapsedMs);
    void closeOutput();
    HRESULT readPackets(QList<QByteArray> *chunks);
    qint64 elapsedMs() const { return unbiasedInterruptTimeMs() - m_startMs; }

    ComPtr<IMMDeviceEnumerator> m_enumerator;
    ComPtr<DefaultOutputWatcher> m_watcher;
    ComPtr<IAudioClient> m_client;
    ComPtr<IAudioCaptureClient> m_capture;
    int m_bytesPerFrame = 0;
    LoopbackPcm m_pcm;
    LoopbackReopen m_reopen;
    qint64 m_startMs = 0;
};

QString LoopbackCapture::open()
{
    m_startMs = unbiasedInterruptTimeMs();
    HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator),
                                      nullptr,
                                      CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&m_enumerator));
    if (FAILED(result)) {
        return captureError(result);
    }
    m_watcher = Microsoft::WRL::Make<DefaultOutputWatcher>();
    if (!m_watcher || !m_watcher->changed()) {
        m_watcher.Reset();
        return captureError(E_OUTOFMEMORY);
    }
    result = m_enumerator->RegisterEndpointNotificationCallback(m_watcher.Get());
    if (FAILED(result)) {
        m_watcher.Reset();
        return QStringLiteral("Could not follow the default sound output: %1")
            .arg(audioErrorText(result, AudioEndpoint::SoundOutput));
    }
    result = openDefaultOutput();
    if (result == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
        return QStringLiteral("There is no sound output to capture system audio from.");
    }
    return FAILED(result) ? captureError(result) : QString();
}

HRESULT LoopbackCapture::openDefaultOutput()
{
    ComPtr<IMMDevice> device;
    HRESULT result = m_enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(result)) {
        return result;
    }
    ComPtr<IAudioClient> client;
    result = device->Activate(__uuidof(IAudioClient),
                              CLSCTX_INPROC_SERVER,
                              nullptr,
                              reinterpret_cast<void **>(client.GetAddressOf()));
    if (FAILED(result)) {
        return result;
    }
    WAVEFORMATEX *mixFormat = nullptr;
    result = client->GetMixFormat(&mixFormat);
    if (FAILED(result)) {
        return result;
    }
    const auto freeMixFormat = qScopeGuard([mixFormat] { CoTaskMemFree(mixFormat); });
    const QAudioFormat format = audioFormatForWave(*mixFormat);
    if (!format.isValid()) {
        return AUDCLNT_E_UNSUPPORTED_FORMAT;
    }
    ComPtr<IAudioCaptureClient> capture;
    if (FAILED(result = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                           AUDCLNT_STREAMFLAGS_LOOPBACK,
                                           kCaptureBufferDuration,
                                           0,
                                           mixFormat,
                                           nullptr))
        || FAILED(result = client->GetService(IID_PPV_ARGS(&capture)))
        || FAILED(result = client->Start())) {
        return result;
    }
    m_client = client;
    m_capture = capture;
    m_bytesPerFrame = mixFormat->nBlockAlign;
    m_pcm.useFormat(format);
    return S_OK;
}

QString LoopbackCapture::reopenDefaultOutput(qint64 elapsedMs)
{
    const HRESULT result = openDefaultOutput();
    if (SUCCEEDED(result)) {
        return {};
    }
    const auto refusal = result == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) ? LoopbackReopen::Refusal::NoOutput
                                                                       : LoopbackReopen::Refusal::Failed;
    if (m_reopen.retries(refusal, elapsedMs)) {
        return {};
    }
    return QStringLiteral("System audio capture stopped: %1")
        .arg(audioErrorText(result, AudioEndpoint::SoundOutput));
}

QString LoopbackCapture::read(QList<QByteArray> *chunks)
{
    if (m_capture) {
        const HRESULT result = readPackets(chunks);
        // Removing the output or changing its format ends its stream.
        if (result == AUDCLNT_E_DEVICE_INVALIDATED) {
            closeOutput();
        } else if (FAILED(result)) {
            return QStringLiteral("System audio capture stopped: %1")
                .arg(audioErrorText(result, AudioEndpoint::SoundOutput));
        }
    }
    // The old default output's last audio is read before moving on, and the
    // change is taken before any reopen, so closing for it gives the new
    // default output an allowance of its own.
    if (m_watcher->takeChange()) {
        closeOutput();
    }
    const qint64 nowMs = elapsedMs();
    if (!m_capture) {
        if (const QString error = reopenDefaultOutput(nowMs); !error.isEmpty()) {
            return error;
        }
    }
    if (chunks->isEmpty()) {
        *chunks = m_pcm.silenceUntil(nowMs);
    }
    return {};
}

void LoopbackCapture::drain(QList<QByteArray> *chunks)
{
    if (m_capture) {
        readPackets(chunks);
    }
    if (chunks->isEmpty()) {
        *chunks = m_pcm.silenceUntil(elapsedMs());
    }
}

void LoopbackCapture::closeOutput()
{
    if (m_client) {
        m_client->Stop();
    }
    m_capture.Reset();
    m_client.Reset();
    m_reopen.restart();
}

HRESULT LoopbackCapture::readPackets(QList<QByteArray> *chunks)
{
    UINT32 packetFrames = 0;
    HRESULT result;
    while (SUCCEEDED(result = m_capture->GetNextPacketSize(&packetFrames)) && packetFrames > 0) {
        BYTE *data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;
        UINT64 playedAt = 0;
        result = m_capture->GetBuffer(&data, &frames, &flags, nullptr, &playedAt);
        if (FAILED(result)) {
            return result;
        }
        // Where the packet played says what the output dropped before it, such
        // as after a stall long enough to fill its buffer.
        if (!(flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)) {
            *chunks += m_pcm.silenceUntil(
                loopbackPacketMs(elapsedMs(), performanceCounterMs(), qint64(playedAt / kHundredNsPerMs)));
        }
        const qsizetype size = qsizetype(frames) * m_bytesPerFrame;
        const QByteArray pcm = flags & AUDCLNT_BUFFERFLAGS_SILENT
            ? m_pcm.convertSilent(size)
            : m_pcm.convert(QByteArray(reinterpret_cast<const char *>(data), size));
        if (!pcm.isEmpty()) {
            chunks->append(pcm);
        }
        result = m_capture->ReleaseBuffer(frames);
        if (FAILED(result)) {
            return result;
        }
    }
    return result;
}

} // namespace

WinSystemAudioInput::WinSystemAudioInput(QObject *parent)
    : WinCaptureInput(QStringLiteral("System audio"), [] { return std::make_unique<LoopbackCapture>(); }, 0, parent)
{
}

} // namespace speecher
