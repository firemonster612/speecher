#include "platform/win/WinCaptureInput.h"

#include "platform/audio/LoopbackPcm.h"

#include <QEventLoop>
#include <QMetaObject>
#include <QScopeGuard>
#include <QTimer>

#include <ks.h>
#include <ksmedia.h>
#include <objbase.h>

namespace speecher {

QString audioErrorText(HRESULT result, AudioEndpoint endpoint)
{
    const QString device = endpoint == AudioEndpoint::Microphone ? QStringLiteral("microphone")
                                                                 : QStringLiteral("sound output");
    switch (result) {
    case E_ACCESSDENIED:
        if (endpoint == AudioEndpoint::Microphone) {
            return QStringLiteral("Windows does not let apps use the microphone; allow it under Settings > Privacy & "
                                  "security > Microphone.");
        }
        break;
    case AUDCLNT_E_DEVICE_IN_USE:
        return QStringLiteral("Another app is using the %1 exclusively.").arg(device);
    case AUDCLNT_E_DEVICE_INVALIDATED:
        return QStringLiteral("The %1 was removed or changed.").arg(device);
    case AUDCLNT_E_SERVICE_NOT_RUNNING:
        return QStringLiteral("The Windows Audio service is not running.");
    case AUDCLNT_E_UNSUPPORTED_FORMAT:
        return QStringLiteral("The %1 uses a format Speecher cannot read.").arg(device);
    }
    return qt_error_string(int(result));
}

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

DefaultOutputWatcher::DefaultOutputWatcher()
    : m_changed(CreateEventW(nullptr, FALSE, FALSE, nullptr))
{
}

DefaultOutputWatcher::~DefaultOutputWatcher()
{
    if (m_changed) {
        CloseHandle(m_changed);
    }
}

bool DefaultOutputWatcher::takeChange()
{
    return WaitForSingleObject(m_changed, 0) == WAIT_OBJECT_0;
}

STDMETHODIMP DefaultOutputWatcher::OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR)
{
    // Windows reports a change once for each role; capture follows the
    // console role's output.
    if (flow == eRender && role == eConsole) {
        SetEvent(m_changed);
    }
    return S_OK;
}

WinCaptureInput::WinCaptureInput(CaptureWording wording, StreamFactory createStream, int postRollMs, QObject *parent)
    : AudioInput(parent)
    , m_wording(wording)
    , m_createStream(std::move(createStream))
    , m_postRollMs(postRollMs)
{
}

WinCaptureInput::~WinCaptureInput()
{
    endCapture();
}

bool WinCaptureInput::start(QString *error)
{
    if (m_thread.joinable()) {
        return true;
    }
    setEchoCancellationWarning({});
    QString failure;
    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (m_stopEvent) {
        std::promise<QString> opened;
        std::future<QString> done = opened.get_future();
        m_thread = std::thread(&WinCaptureInput::capture, this, m_generation, std::move(opened));
        failure = done.get();
    } else {
        failure = m_wording.startFailed(qt_error_string(int(GetLastError())));
    }
    if (failure.isEmpty()) {
        return true;
    }
    endCapture();
    if (error) {
        *error = failure;
    }
    return false;
}

void WinCaptureInput::stop()
{
    if (m_thread.joinable() && m_postRollMs > 0) {
        // The post-roll's audio is delivered meanwhile.
        QEventLoop loop;
        QTimer::singleShot(m_postRollMs, &loop, &QEventLoop::quit);
        loop.exec();
    }
    endCapture();
    deliverAudio();
}

bool WinCaptureInput::isActive() const
{
    return m_thread.joinable();
}

QString WinCaptureInput::echoCancellationWarning() const
{
    const QMutexLocker locker(&m_mutex);
    return m_echoCancellationWarning;
}

void WinCaptureInput::capture(quint64 generation, std::promise<QString> opened)
{
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const auto uninitialize = qScopeGuard([apartment] {
        if (SUCCEEDED(apartment)) {
            CoUninitialize();
        }
    });
    const std::unique_ptr<WinCaptureStream> stream = m_createStream();
    if (const QString error = stream->open(); !error.isEmpty()) {
        opened.set_value(error);
        return;
    }
    setEchoCancellationWarning(stream->echoCancellationWarning());
    opened.set_value({});

    for (;;) {
        // Polled rather than event-driven, because loopback signals nothing
        // while nothing plays, and the poll that finds no audio is what fills
        // the silence.
        const DWORD woke = WaitForSingleObject(m_stopEvent, DWORD(kLoopbackPollMs));
        QList<QByteArray> chunks;
        if (woke == WAIT_OBJECT_0) {
            stream->drain(&chunks);
            queueAudio(chunks);
            return;
        }
        const QString error = woke == WAIT_FAILED
            ? m_wording.stopped(qt_error_string(int(GetLastError())))
            : stream->read(&chunks);
        queueAudio(chunks);
        setEchoCancellationWarning(stream->echoCancellationWarning());
        if (!error.isEmpty()) {
            QMetaObject::invokeMethod(
                this, [this, error, generation] { fail(generation, error); }, Qt::QueuedConnection);
            return;
        }
    }
}

void WinCaptureInput::queueAudio(const QList<QByteArray> &chunks)
{
    if (chunks.isEmpty()) {
        return;
    }
    {
        const QMutexLocker locker(&m_mutex);
        m_queued += chunks;
    }
    QMetaObject::invokeMethod(this, &WinCaptureInput::deliverAudio, Qt::QueuedConnection);
}

void WinCaptureInput::setEchoCancellationWarning(const QString &warning)
{
    const QMutexLocker locker(&m_mutex);
    m_echoCancellationWarning = warning;
}

// One chunk at a time, so a stop from a handler finds the rest still queued
// and delivers it, in order, before it returns. What is queued after that
// stop is a later capture's, which has a delivery of its own.
void WinCaptureInput::deliverAudio()
{
    const quint64 generation = m_generation;
    while (generation == m_generation) {
        QByteArray chunk;
        {
            const QMutexLocker locker(&m_mutex);
            if (m_queued.isEmpty()) {
                return;
            }
            chunk = m_queued.takeFirst();
        }
        emit audioChunk(chunk);
    }
}

void WinCaptureInput::endCapture()
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

void WinCaptureInput::fail(quint64 generation, const QString &message)
{
    if (generation != m_generation) {
        return;
    }
    endCapture();
    emit failed(message);
}

} // namespace speecher
