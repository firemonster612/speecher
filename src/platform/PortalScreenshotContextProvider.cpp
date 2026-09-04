#include "platform/PortalScreenshotContextProvider.h"

#include "platform/ScreenshotImage.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFile>
#include <QTimer>
#include <QUrl>
#include <QUuid>

namespace speecher {

namespace {

constexpr qsizetype maximumPortalFileSize = 32 * 1024 * 1024;
constexpr int screenshotTimeoutMs = 30'000;

QDBusObjectPath predictedRequestPath(const QString &token)
{
    QString sender = QDBusConnection::sessionBus().baseService();
    sender.remove(0, sender.startsWith(QLatin1Char(':')) ? 1 : 0);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    return QDBusObjectPath(
        QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token));
}
} // namespace

PortalScreenshotContextProvider::PortalScreenshotContextProvider(QObject *parent)
    : ScreenshotContextProvider(parent)
{
    m_requestTimer = new QTimer(this);
    m_requestTimer->setSingleShot(true);
    connect(m_requestTimer, &QTimer::timeout,
            this, &PortalScreenshotContextProvider::handleTimeout);
}

void PortalScreenshotContextProvider::capture()
{
    cancel();
    const quint64 generation = m_generation;

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
    const QString token = QStringLiteral("speecher_%1").arg(
        QUuid::createUuid().toString(QUuid::Id128));
    options.insert(QStringLiteral("handle_token"), token);
    m_requestPath = predictedRequestPath(token);
    m_responseTracker.begin(m_requestPath);
    const bool connected = QDBusConnection::sessionBus().connect(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        QString(),
        QStringLiteral("org.freedesktop.portal.Request"),
        QStringLiteral("Response"),
        this,
        SLOT(handleResponse(uint,QVariantMap)));
    if (!connected) {
        m_requestPath = {};
        emit failed(QStringLiteral("Could not watch the screenshot portal request"));
        return;
    }
    auto *watcher = new QDBusPendingCallWatcher(
        portal.asyncCall(QStringLiteral("Screenshot"), QString(), options),
        this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation] {
        const QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        watcher->deleteLater();
        if (generation != m_generation) {
            if (!reply.isError()) {
                QDBusInterface request(QStringLiteral("org.freedesktop.portal.Desktop"),
                                       reply.value().path(),
                                       QStringLiteral("org.freedesktop.portal.Request"),
                                       QDBusConnection::sessionBus());
                request.asyncCall(QStringLiteral("Close"));
            }
            return;
        }
        if (reply.isError()) {
            disconnectRequest();
            emit failed(QStringLiteral("Screenshot capture was not available: %1")
                            .arg(reply.error().message()));
            return;
        }

        m_requestPath = reply.value();
        if (const auto response = m_responseTracker.resolve(m_requestPath)) {
            processResponse(*response);
        }
    });
    m_requestTimer->start(screenshotTimeoutMs);
}

void PortalScreenshotContextProvider::cancel()
{
    ++m_generation;
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
    const auto matched = m_responseTracker.observe(message().path(), response, results);
    if (!matched) {
        return;
    }
    processResponse(*matched);
}

void PortalScreenshotContextProvider::handleTimeout()
{
    ++m_generation;
    if (!m_requestPath.path().isEmpty()) {
        QDBusInterface request(QStringLiteral("org.freedesktop.portal.Desktop"),
                               m_requestPath.path(),
                               QStringLiteral("org.freedesktop.portal.Request"),
                               QDBusConnection::sessionBus());
        request.asyncCall(QStringLiteral("Close"));
    }
    disconnectRequest();
    emit failed(QStringLiteral("Screenshot capture timed out"));
}

void PortalScreenshotContextProvider::processResponse(const PortalResponse &response)
{
    disconnectRequest();
    if (response.status != 0) {
        emit failed(response.status == 1
                        ? QStringLiteral("Screenshot capture was cancelled")
                        : QStringLiteral("Screenshot capture failed"));
        return;
    }

    const QUrl uri(response.results.value(QStringLiteral("uri")).toString());
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
    m_requestTimer->stop();
    QDBusConnection::sessionBus().disconnect(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        QString(),
        QStringLiteral("org.freedesktop.portal.Request"),
        QStringLiteral("Response"),
        this,
        SLOT(handleResponse(uint,QVariantMap)));
    m_requestPath = {};
    m_responseTracker.clear();
}

} // namespace speecher
