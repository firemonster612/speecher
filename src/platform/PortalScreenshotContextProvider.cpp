#include "platform/PortalScreenshotContextProvider.h"

#include "platform/PortalRequest.h"
#include "platform/ScreenshotImage.h"

#include <QUrl>

namespace speecher {

namespace {

constexpr int screenshotTimeoutMs = 30'000;

} // namespace

PortalScreenshotContextProvider::PortalScreenshotContextProvider(QObject *parent)
    : ScreenshotContextProvider(parent)
{
}

void PortalScreenshotContextProvider::capture()
{
    cancel();
    auto *request = new PortalRequest(this);
    m_request = request;
    connect(request, &PortalRequest::answered, this, &PortalScreenshotContextProvider::processResponse);
    connect(request, &PortalRequest::failed, this, [this](const QString &error) {
        emit failed(QStringLiteral("Screenshot capture was not available: %1").arg(error));
    });
    connect(request, &PortalRequest::timedOut, this, [this] {
        emit failed(QStringLiteral("Screenshot capture timed out"));
    });
    QVariantMap options;
    options.insert(QStringLiteral("interactive"), false);
    request->send(QStringLiteral("org.freedesktop.portal.Screenshot"), QStringLiteral("Screenshot"),
                  {QString(), options}, screenshotTimeoutMs);
}

void PortalScreenshotContextProvider::cancel()
{
    ++m_generation;
    if (m_request) {
        m_request->cancel();
    }
}

void PortalScreenshotContextProvider::processResponse(uint response, const QVariantMap &results)
{
    if (response != 0) {
        emit failed(response == 1
                        ? QStringLiteral("Screenshot capture was cancelled")
                        : QStringLiteral("Screenshot capture failed"));
        return;
    }

    const QUrl uri(results.value(QStringLiteral("uri")).toString());
    const QString path = uri.toLocalFile();
    if (path.isEmpty()) {
        emit failed(QStringLiteral("The screenshot portal returned an unreadable location"));
        return;
    }

    const quint64 generation = m_generation;
    normalizeScreenshotFile(path, this, [this, generation](const ScreenshotResult &result) {
        if (generation != m_generation) {
            return;
        }
        if (!result.error.isEmpty()) {
            emit failed(result.error);
            return;
        }
        emit captured(result.png, QStringLiteral("image/png"));
    });
}

} // namespace speecher
