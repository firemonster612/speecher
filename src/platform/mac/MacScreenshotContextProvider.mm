#include "platform/mac/MacScreenshotContextProvider.h"

#include "platform/ScreenshotImage.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QUuid>

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

#include <optional>

namespace speecher {

namespace {

constexpr auto screenCaptureTool = "/usr/sbin/screencapture";
constexpr qsizetype maximumCaptureFileSize = 32 * 1024 * 1024;

QString screenRecordingHint()
{
    return QStringLiteral(
        "Screen capture failed. Allow Speecher under Privacy & Security > Screen Recording.");
}

std::optional<CGRect> frontmostWindowBounds()
{
    NSRunningApplication *frontmost = NSWorkspace.sharedWorkspace.frontmostApplication;
    if (!frontmost) {
        return std::nullopt;
    }
    const pid_t targetPid = frontmost.processIdentifier;
    CFArrayRef windows = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
        kCGNullWindowID);
    if (!windows) {
        return std::nullopt;
    }
    std::optional<CGRect> bounds;
    for (NSDictionary *window in (__bridge NSArray *)windows) {
        if ([window[(__bridge NSString *)kCGWindowOwnerPID] intValue] != targetPid
            || [window[(__bridge NSString *)kCGWindowLayer] intValue] != 0
            || ![window[(__bridge NSString *)kCGWindowIsOnscreen] boolValue]) {
            continue;
        }
        CGRect candidate;
        if (CGRectMakeWithDictionaryRepresentation(
                (__bridge CFDictionaryRef)window[(__bridge NSString *)kCGWindowBounds],
                &candidate)
            && candidate.size.width > 0 && candidate.size.height > 0) {
            bounds = candidate;
            break;
        }
    }
    CFRelease(windows);
    return bounds;
}

} // namespace

MacScreenshotContextProvider::MacScreenshotContextProvider(QObject *parent)
    : ScreenshotContextProvider(parent)
{
}

void MacScreenshotContextProvider::capture()
{
    cancel();

    // Without the grant screencapture still runs and still writes a file, it
    // just hands back the desktop picture. Asking first turns a plausible but
    // useless screenshot into an honest failure.
    if (!CGPreflightScreenCaptureAccess() && !CGRequestScreenCaptureAccess()) {
        emit failed(screenRecordingHint());
        return;
    }

    const std::optional<CGRect> bounds = frontmostWindowBounds();
    if (!bounds) {
        emit failed(QStringLiteral("Could not find the target window to capture"));
        return;
    }

    m_capturePath = QDir::temp().filePath(
        QStringLiteral("speecher_%1.png").arg(QUuid::createUuid().toString(QUuid::Id128)));
    m_capture = new QProcess(this);
    connect(m_capture,
            &QProcess::finished,
            this,
            [this](int exitCode, QProcess::ExitStatus) { finish(exitCode); });
    connect(m_capture, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (!m_capture) {
            return;
        }
        // errorOccurred can be followed by finished for the same process; the
        // disconnect keeps that second callback from running against a
        // half-torn-down capture.
        m_capture->disconnect(this);
        m_capture->deleteLater();
        m_capture = nullptr;
        discardCaptureFile();
        emit failed(QStringLiteral("Could not run the macOS screen capture tool"));
    });
    const QString region = QStringLiteral("%1,%2,%3,%4")
                               .arg(qRound(bounds->origin.x))
                               .arg(qRound(bounds->origin.y))
                               .arg(qRound(bounds->size.width))
                               .arg(qRound(bounds->size.height));
    // -x keeps the capture silent; the shutter sound during dictation would land
    // in the recording. -R confines context to the target instead of recording
    // every display.
    m_capture->start(QString::fromLatin1(screenCaptureTool),
                     {QStringLiteral("-x"),
                      QStringLiteral("-t"),
                      QStringLiteral("png"),
                      QStringLiteral("-R"),
                      region,
                      m_capturePath});
}

void MacScreenshotContextProvider::cancel()
{
    if (m_capture) {
        m_capture->disconnect(this);
        m_capture->kill();
        m_capture->deleteLater();
        m_capture = nullptr;
    }
    discardCaptureFile();
}

void MacScreenshotContextProvider::discardCaptureFile()
{
    if (m_capturePath.isEmpty()) {
        return;
    }
    QFile::remove(m_capturePath);
    m_capturePath.clear();
}

void MacScreenshotContextProvider::finish(int exitCode)
{
    if (!m_capture) {
        return;
    }
    m_capture->deleteLater();
    m_capture = nullptr;

    // A denied Screen Recording grant shows up either as a non-zero exit or as
    // an empty file, depending on the macOS version.
    QFile file(m_capturePath);
    if (exitCode != 0 || !file.open(QIODevice::ReadOnly) || file.size() == 0
        || file.size() > maximumCaptureFileSize) {
        file.close();
        discardCaptureFile();
        emit failed(screenRecordingHint());
        return;
    }
    const QByteArray source = file.readAll();
    file.close();
    discardCaptureFile();

    const QByteArray png = normalizedScreenshot(source);
    if (png.isEmpty()) {
        emit failed(QStringLiteral("The captured screenshot format was not supported"));
        return;
    }
    emit captured(png, QStringLiteral("image/png"));
}

} // namespace speecher
