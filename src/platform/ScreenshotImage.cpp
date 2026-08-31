#include "platform/ScreenshotImage.h"

#include <QBuffer>
#include <QImage>
#include <QImageReader>

namespace speecher {

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

} // namespace speecher
