#include "platform/win/WinScreenshotContextProvider.h"

#include <QBuffer>
#include <QImage>
#include <QThread>

#include <windows.h>

#include <memory>

namespace speecher {
namespace {

constexpr qsizetype maximumCaptureSize = 32 * 1024 * 1024;

struct ScreenshotResult {
    QByteArray png;
    QString error;
};

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
    QByteArray png;
    if (printed) {
        const QImage image(static_cast<uchar *>(bits),
                           width,
                           height,
                           width * 4,
                           QImage::Format_RGB32);
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
    }
    DeleteObject(bitmap);
    DeleteDC(memory);

    if (!printed || png.isEmpty()) {
        return {{}, QStringLiteral("Windows could not capture the target window")};
    }
    if (png.size() > maximumCaptureSize) {
        return {{}, QStringLiteral("The target window screenshot is larger than 32 MiB")};
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
