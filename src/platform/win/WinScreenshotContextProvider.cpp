#include "platform/win/WinScreenshotContextProvider.h"

#include "platform/ScreenshotImage.h"

#include <QCoreApplication>
#include <QImage>
#include <QMutex>
#include <QThread>

#include <windows.h>

#include <memory>

namespace speecher {
namespace {

// What a capture's worker and the quitting application share. The worker can
// sit in PrintWindow for as long as the target window doesn't answer, so
// quitting must not wait for it there. It waits only while the worker
// normalizes: smooth scaling runs on Qt's shared thread pool, which
// application teardown destroys.
struct CaptureState {
    QMutex mutex;
    bool quitting = false;
    ScreenshotResult result;
};

// The window's pixels, or a null image with error. PrintWindow has the
// window's own thread paint them, so this blocks while that thread hangs.
QImage printWindow(HWND window, QString *error)
{
    RECT bounds{};
    if (!window || !GetWindowRect(window, &bounds)) {
        *error = QStringLiteral("Could not find the target window to capture");
        return {};
    }
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if (width <= 0 || height <= 0) {
        *error = QStringLiteral("The target window has no visible area to capture");
        return {};
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void *bits = nullptr;
    const HDC screen = GetDC(nullptr);
    const HDC memory = CreateCompatibleDC(screen);
    const HBITMAP bitmap = CreateDIBSection(
        screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!memory || !bitmap || !bits) {
        if (bitmap) {
            DeleteObject(bitmap);
        }
        if (memory) {
            DeleteDC(memory);
        }
        *error = QStringLiteral("Windows could not create the screenshot buffer");
        return {};
    }

    const HGDIOBJ previous = SelectObject(memory, bitmap);
    const BOOL printed = PrintWindow(window, memory, PW_RENDERFULLCONTENT);
    SelectObject(memory, previous);
    // The image borrows the bitmap's pixels, so it is copied before the
    // bitmap goes.
    const QImage image = printed
        ? QImage(static_cast<uchar *>(bits), width, height, width * 4, QImage::Format_RGB32).copy()
        : QImage();
    DeleteObject(bitmap);
    DeleteDC(memory);
    if (image.isNull()) {
        *error = QStringLiteral("Windows could not capture the target window");
    }
    return image;
}

ScreenshotResult normalizedCapture(const QImage &image)
{
    const QByteArray png = normalizedScreenshot(image);
    if (png.isEmpty()) {
        return {{}, QStringLiteral("Windows could not capture the target window")};
    }
    return {png, {}};
}

} // namespace

WinScreenshotContextProvider::WinScreenshotContextProvider(QObject *parent)
    : ScreenshotContextProvider(parent)
{
}

void WinScreenshotContextProvider::capture()
{
    cancel();
    // A worker that hasn't finished, perhaps waiting on a window that doesn't
    // answer, is left to; starting another for every Dictation Session would
    // pile them up.
    if (m_worker && m_worker->isRunning()) {
        emit failed(QStringLiteral("Windows is still capturing an earlier window"));
        return;
    }
    const HWND window = GetForegroundWindow();
    auto state = std::make_shared<CaptureState>();
    QThread *thread = QThread::create([window, state] {
        QString error;
        const QImage image = printWindow(window, &error);
        const QMutexLocker locker(&state->mutex);
        if (state->quitting) {
            return;
        }
        state->result = image.isNull() ? ScreenshotResult{{}, error} : normalizedCapture(image);
    });
    m_worker = thread;
    m_wanted = true;
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(qApp, &QCoreApplication::aboutToQuit, thread, [state] {
        const QMutexLocker locker(&state->mutex);
        state->quitting = true;
    });
    connect(thread, &QThread::finished, this, [this, thread, state] {
        if (m_worker != thread || !m_wanted) {
            return;
        }
        m_wanted = false;
        if (!state->result.error.isEmpty()) {
            emit failed(state->result.error);
            return;
        }
        emit captured(state->result.png, QStringLiteral("image/png"));
    });
    thread->start();
}

void WinScreenshotContextProvider::cancel()
{
    m_wanted = false;
}

} // namespace speecher
