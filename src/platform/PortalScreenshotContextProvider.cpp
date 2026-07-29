#include "platform/PortalScreenshotContextProvider.h"

#include <QBuffer>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFile>
#include <QImage>
#include <QUrl>
#include <QUuid>

namespace speecher {

namespace {

constexpr qsizetype maximumPortalFileSize = 32 * 1024 * 1024;

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

} // namespace

PortalScreenshotContextProvider::PortalScreenshotContextProvider(QObject *parent)
    : ScreenshotContextProvider(parent)
{
}

void PortalScreenshotContextProvider::capture()
{
    cancel();
    m_cancelled = false;

    QDBusInterface portal(QStringLiteral("org.freedesktop.portal.Desktop"),
                          QStringLiteral("/org/freedesktop/portal/desktop"),
                          QStringLiteral("org.freedesktop.portal.Screenshot"),
                          QDBusConnection::sessionBus());
    if (!portal.isValid()) {
        emit failed(QStringLiteral("The desktop screenshot portal is unavailable"));
        return;
    }

    QVariantMap options;
    options.insert(QStringLiteral("interactive"), false);
    options.insert(QStringLiteral("handle_token"),
                   QStringLiteral("speecher_%1").arg(
                       QUuid::createUuid().toString(QUuid::Id128)));
    auto *watcher = new QDBusPendingCallWatcher(
        portal.asyncCall(QStringLiteral("Screenshot"), QString(), options),
        this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        const QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        watcher->deleteLater();
        if (m_cancelled) {
            return;
        }
        if (reply.isError()) {
            emit failed(QStringLiteral("Screenshot capture was not available: %1")
                            .arg(reply.error().message()));
            return;
        }

        m_requestPath = reply.value();
        const bool connected = QDBusConnection::sessionBus().connect(
            QStringLiteral("org.freedesktop.portal.Desktop"),
            m_requestPath.path(),
            QStringLiteral("org.freedesktop.portal.Request"),
            QStringLiteral("Response"),
            this,
            SLOT(handleResponse(uint,QVariantMap)));
        if (!connected) {
            m_requestPath = {};
            emit failed(QStringLiteral("Could not watch the screenshot portal request"));
        }
    });
}

void PortalScreenshotContextProvider::cancel()
{
    m_cancelled = true;
    if (!m_requestPath.path().isEmpty()) {
        QDBusInterface request(QStringLiteral("org.freedesktop.portal.Desktop"),
                               m_requestPath.path(),
                               QStringLiteral("org.freedesktop.portal.Request"),
                               QDBusConnection::sessionBus());
        request.asyncCall(QStringLiteral("Close"));
    }
    disconnectRequest();
}

void PortalScreenshotContextProvider::handleResponse(uint response, const QVariantMap &results)
{
    disconnectRequest();
    if (m_cancelled) {
        return;
    }
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

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > maximumPortalFileSize) {
        file.close();
        QFile::remove(path);
        emit failed(QStringLiteral("The captured screenshot could not be read"));
        return;
    }
    const QByteArray source = file.readAll();
    file.close();
    QFile::remove(path);

    const QByteArray png = normalizedScreenshot(source);
    if (png.isEmpty()) {
        emit failed(QStringLiteral("The captured screenshot format was not supported"));
        return;
    }
    emit captured(png, QStringLiteral("image/png"));
}

void PortalScreenshotContextProvider::disconnectRequest()
{
    if (m_requestPath.path().isEmpty()) {
        return;
    }
    QDBusConnection::sessionBus().disconnect(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        m_requestPath.path(),
        QStringLiteral("org.freedesktop.portal.Request"),
        QStringLiteral("Response"),
        this,
        SLOT(handleResponse(uint,QVariantMap)));
    m_requestPath = {};
}

} // namespace speecher
