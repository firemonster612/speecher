#include "platform/ScreenshotImage.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QThread>

#include <memory>

namespace speecher {

namespace {

ScreenshotResult readScreenshotFile(const QString &path)
{
    constexpr qsizetype maximumFileSize = 32 * 1024 * 1024;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > maximumFileSize) {
        file.close();
        QFile::remove(path);
        return {{}, QStringLiteral("The captured screenshot could not be read")};
    }
    const QByteArray source = file.readAll();
    file.close();
    QFile::remove(path);

    const QByteArray png = normalizedScreenshot(source);
    if (png.isEmpty()) {
        return {{}, QStringLiteral("The captured screenshot format was not supported")};
    }
    return {png, {}};
}

} // namespace

QByteArray normalizedScreenshot(const QByteArray &source)
{
    QBuffer input;
    input.setData(source);
    if (!input.open(QIODevice::ReadOnly)) {
        return {};
    }
    QImageReader reader(&input);
    const QSize size = reader.size();
    constexpr int maximumSourceEdge = 8192;
    constexpr qint64 maximumSourcePixels = 32 * 1024 * 1024;
    if (!size.isValid()
        || size.width() > maximumSourceEdge
        || size.height() > maximumSourceEdge
        || qint64(size.width()) * size.height() > maximumSourcePixels) {
        return {};
    }
    QImage image = reader.read();
    if (image.isNull()) {
        return {};
    }

    constexpr int maximumEdge = 2560;
    if (image.width() > maximumEdge || image.height() > maximumEdge) {
        image = image.scaled(maximumEdge,
                             maximumEdge,
                             Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    }

    QByteArray result;
    QBuffer output(&result);
    if (!output.open(QIODevice::WriteOnly) || !image.save(&output, "PNG")) {
        return {};
    }
    return result;
}

void normalizeScreenshotFile(const QString &path,
                             QObject *receiver,
                             std::function<void(const ScreenshotResult &)> done)
{
    auto result = std::make_shared<ScreenshotResult>();
    QThread *thread = QThread::create([path, result] { *result = readScreenshotFile(path); });
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    // Smooth scaling runs on Qt's shared thread pool, which application
    // teardown destroys, so the worker has to finish first.
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, thread, [thread] { thread->wait(); });
    QObject::connect(thread, &QThread::finished, receiver,
                     [result, done = std::move(done)] { done(*result); });
    thread->start();
}

} // namespace speecher
