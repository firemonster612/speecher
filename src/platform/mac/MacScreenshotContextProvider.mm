#include "platform/mac/MacScreenshotContextProvider.h"

#include "platform/ScreenshotImage.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QUuid>

#import <CoreGraphics/CoreGraphics.h>

namespace speecher {

namespace {

constexpr auto screenCaptureTool = "/usr/sbin/screencapture";
constexpr qsizetype maximumCaptureFileSize = 32 * 1024 * 1024;

QString screenRecordingHint()
{
    return QStringLiteral(
        "Screen capture failed. Allow Speecher under Privacy & Security > Screen Recording.");
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
    if (!CGPreflightScreenCaptureAccess()) {
        emit failed(screenRecordingHint());
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
    // -x keeps the capture silent; the shutter sound during dictation would land
    // in the recording.
    m_capture->start(QString::fromLatin1(screenCaptureTool),
                     {QStringLiteral("-x"), QStringLiteral("-t"), QStringLiteral("png"), m_capturePath});
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
