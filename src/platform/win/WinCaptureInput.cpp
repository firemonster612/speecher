#include "platform/win/WinCaptureInput.h"

#include "platform/audio/LoopbackPcm.h"

#include <QMetaObject>
#include <QScopeGuard>

#include <objbase.h>

namespace speecher {

WinCaptureInput::WinCaptureInput(StreamFactory createStream, QObject *parent)
    : AudioInput(parent)
    , m_createStream(std::move(createStream))
{
}

WinCaptureInput::~WinCaptureInput()
{
    stop();
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
        result.error = QStringLiteral("Could not start audio capture: %1").arg(qt_error_string(int(GetLastError())));
    }
    m_echoCancellationWarning = result.echoCancellationWarning;
    if (result.error.isEmpty()) {
        return true;
    }
    stop();
    if (error) {
        *error = result.error;
    }
    return false;
}

void WinCaptureInput::stop()
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
        if (woke == WAIT_OBJECT_0) {
            return;
        }
        QList<QByteArray> chunks;
        const QString error = woke == WAIT_FAILED
            ? QStringLiteral("Audio capture stopped: %1").arg(qt_error_string(int(GetLastError())))
            : stream->read(&chunks);
        if (!chunks.isEmpty()) {
            QMetaObject::invokeMethod(
                this,
                [this, chunks, generation] {
                    for (const QByteArray &chunk : chunks) {
                        if (generation != m_generation) {
                            return;
                        }
                        emit audioChunk(chunk);
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

void WinCaptureInput::fail(quint64 generation, const QString &message)
{
    if (generation != m_generation) {
        return;
    }
    stop();
    emit failed(message);
}

} // namespace speecher
