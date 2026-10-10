#include "platform/win/WinCaptureInput.h"

#include "platform/audio/LoopbackPcm.h"

#include <QEventLoop>
#include <QMetaObject>
#include <QScopeGuard>
#include <QTimer>

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

WinCaptureInput::WinCaptureInput(const QString &name, StreamFactory createStream, int postRollMs, QObject *parent)
    : AudioInput(parent)
    , m_name(name)
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
    Opened result;
    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (m_stopEvent) {
        std::promise<Opened> opened;
        std::future<Opened> done = opened.get_future();
        m_thread = std::thread(&WinCaptureInput::capture, this, m_generation, std::move(opened));
        result = done.get();
    } else {
        result.error = QStringLiteral("%1 capture could not start: %2")
                           .arg(m_name, qt_error_string(int(GetLastError())));
    }
    m_echoCancellationWarning = result.echoCancellationWarning;
    if (result.error.isEmpty()) {
        return true;
    }
    endCapture();
    if (error) {
        *error = result.error;
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
    return m_echoCancellationWarning;
}

void WinCaptureInput::capture(quint64 generation, std::promise<Opened> opened)
{
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const auto uninitialize = qScopeGuard([apartment] {
        if (SUCCEEDED(apartment)) {
            CoUninitialize();
        }
    });
    const std::unique_ptr<WinCaptureStream> stream = m_createStream();
    if (const QString error = stream->open(); !error.isEmpty()) {
        opened.set_value({error, {}});
        return;
    }
    opened.set_value({{}, stream->echoCancellationWarning()});

    for (;;) {
        // Polled rather than event-driven, because loopback signals nothing
        // while nothing plays, and the poll that finds no audio is what fills
        // the silence.
        const DWORD woke = WaitForSingleObject(m_stopEvent, DWORD(kLoopbackPollMs));
        QList<QByteArray> chunks;
        const QString error = woke == WAIT_FAILED
            ? QStringLiteral("%1 capture stopped: %2").arg(m_name, qt_error_string(int(GetLastError())))
            : stream->read(&chunks);
        queueAudio(chunks);
        // The read after a stop takes what the stream still holds; a failure
        // of it is no longer worth reporting.
        if (woke == WAIT_OBJECT_0) {
            return;
        }
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
        const QMutexLocker locker(&m_queuedMutex);
        m_queued += chunks;
    }
    QMetaObject::invokeMethod(this, &WinCaptureInput::deliverAudio, Qt::QueuedConnection);
}

void WinCaptureInput::deliverAudio()
{
    QList<QByteArray> chunks;
    {
        const QMutexLocker locker(&m_queuedMutex);
        chunks.swap(m_queued);
    }
    for (const QByteArray &chunk : std::as_const(chunks)) {
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
