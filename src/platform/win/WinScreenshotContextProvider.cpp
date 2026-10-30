#include "platform/win/WinScreenshotContextProvider.h"

#include "platform/ScreenshotImage.h"

#include <QCoreApplication>
#include <QImage>
#include <QThread>

#include <windows.h>

#include <memory>

namespace speecher {
namespace {

ScreenshotResult captureWindow(HWND window)
{
    RECT bounds{};
    if (!window || !GetWindowRect(window, &bounds)) {
        return {{}, QStringLiteral("Could not find the target window to capture")};
    }
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if (width <= 0 || height <= 0) {
        return {{}, QStringLiteral("The target window has no visible area to capture")};
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
        return {{}, QStringLiteral("Windows could not create the screenshot buffer")};
    }

    const HGDIOBJ previous = SelectObject(memory, bitmap);
    const BOOL printed = PrintWindow(window, memory, PW_RENDERFULLCONTENT);
    SelectObject(memory, previous);
    // The image borrows the bitmap's pixels, so it is normalized before the
    // bitmap goes.
    const QByteArray png = printed
        ? normalizedScreenshot(QImage(static_cast<uchar *>(bits), width, height, width * 4, QImage::Format_RGB32))
        : QByteArray();
    DeleteObject(bitmap);
    DeleteDC(memory);

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

WinScreenshotContextProvider::~WinScreenshotContextProvider()
{
    cancel();
}

void WinScreenshotContextProvider::capture()
{
    cancel();
    const HWND window = GetForegroundWindow();
    auto result = std::make_shared<ScreenshotResult>();
    QThread *thread = QThread::create([window, result] {
        *result = captureWindow(window);
    });
    m_capture = thread;
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    // Smooth scaling runs on Qt's shared thread pool, which application
    // teardown destroys, so the worker has to finish first.
    connect(qApp, &QCoreApplication::aboutToQuit, thread, [thread] { thread->wait(); });
    connect(thread, &QThread::finished, this, [this, thread, result] {
        if (m_capture != thread) {
            return;
        }
        m_capture = nullptr;
        if (!result->error.isEmpty()) {
            emit failed(result->error);
            return;
        }
        emit captured(result->png, QStringLiteral("image/png"));
    });
    thread->start();
}

void WinScreenshotContextProvider::cancel()
{
    if (!m_capture) {
        return;
    }
    m_capture->disconnect(this);
    m_capture->requestInterruption();
    m_capture = nullptr;
}

} // namespace speecher
