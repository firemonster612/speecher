#include "platform/ScreenshotImage.h"

#include <QBuffer>
#include <QImage>

namespace speecher {

QByteArray normalizedScreenshot(const QByteArray &source)
{
    QImage image;
    if (!image.loadFromData(source)) {
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
