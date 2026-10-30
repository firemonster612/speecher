#include "platform/PortalRequest.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QTimer>
#include <QUuid>

#include <utility>

namespace speecher {
namespace {

constexpr auto requestInterface = "org.freedesktop.portal.Request";

QDBusObjectPath predictedRequestPath(const QString &token)
{
    QString sender = QDBusConnection::sessionBus().baseService();
    sender.remove(0, sender.startsWith(QLatin1Char(':')) ? 1 : 0);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    return QDBusObjectPath(
        QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token));
}

// QDBusInterface introspects the remote object synchronously, so every portal
// call goes out as a plain message instead.
void closeRequest(const QString &path)
{
    QDBusConnection::sessionBus().asyncCall(QDBusMessage::createMethodCall(
        QString::fromLatin1(portalService), path, QString::fromLatin1(requestInterface),
        QStringLiteral("Close")));
}

} // namespace

PortalRequest::PortalRequest(QObject *parent)
    : QObject(parent)
    , m_deadline(new QTimer(this))
{
    m_deadline->setSingleShot(true);
    connect(m_deadline, &QTimer::timeout, this, [this] {
        cancel();
        emit timedOut();
    });
}

void PortalRequest::send(const QString &interface, const QString &method, QVariantList arguments, int timeoutMs)
{
    const QString token = QStringLiteral("speecher_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    QVariantMap options = arguments.last().toMap();
    options.insert(QStringLiteral("handle_token"), token);
    arguments.last() = options;
    m_path = predictedRequestPath(token);

    // Response is watched on every path: the handle is only a prediction
    // until the call's reply confirms it.
    if (!QDBusConnection::sessionBus().connect(
            QString::fromLatin1(portalService), QString(), QString::fromLatin1(requestInterface),
            QStringLiteral("Response"), this, SLOT(handleResponse(uint,QVariantMap)))) {
        finish();
        emit failed(QStringLiteral("Could not watch the portal request"));
        return;
    }
    QDBusMessage call = QDBusMessage::createMethodCall(
        QString::fromLatin1(portalService), QString::fromLatin1(portalPath), interface, method);
    call.setArguments(arguments);
    m_call = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(m_call, &QDBusPendingCallWatcher::finished, this, &PortalRequest::handleReply);
    m_deadline->start(timeoutMs);
}

void PortalRequest::cancel()
{
    if (m_finished) {
        return;
    }
    if (m_call) {
        // The portal may still be opening the request; close it once the
        // reply names it, after this object is gone.
        QDBusPendingCallWatcher *call = std::exchange(m_call, nullptr);
        call->disconnect(this);
        call->setParent(nullptr);
        connect(call, &QDBusPendingCallWatcher::finished, call, [call] {
            const QDBusPendingReply<QDBusObjectPath> reply = *call;
            if (!reply.isError()) {
                closeRequest(reply.value().path());
            }
            call->deleteLater();
        });
    } else {
        closeRequest(m_path.path());
    }
    finish();
}

void PortalRequest::handleReply()
{
    const QDBusPendingReply<QDBusObjectPath> reply = *m_call;
    std::exchange(m_call, nullptr)->deleteLater();
    if (reply.isError()) {
        finish();
        emit failed(reply.error().message());
        return;
    }
    m_path = reply.value();
    const auto early = m_earlyResponses.constFind(m_path.path());
    if (early == m_earlyResponses.cend()) {
        m_earlyResponses.clear();
        return;
    }
    const Response response = early.value();
    finish();
    emit answered(response.status, response.results);
}

void PortalRequest::handleResponse(uint response, const QVariantMap &results)
{
    // A Response D-Bus queued before finish() disconnected it.
    if (m_finished) {
        return;
    }
    const QString path = message().path();
    if (path != m_path.path()) {
        if (m_call) {
            m_earlyResponses.insert(path, {response, results});
        }
        return;
    }
    finish();
    emit answered(response, results);
}

void PortalRequest::finish()
{
    m_finished = true;
    m_deadline->stop();
    delete std::exchange(m_call, nullptr);
    m_earlyResponses.clear();
    QDBusConnection::sessionBus().disconnect(
        QString::fromLatin1(portalService), QString(), QString::fromLatin1(requestInterface),
        QStringLiteral("Response"), this, SLOT(handleResponse(uint,QVariantMap)));
    deleteLater();
}

} // namespace speecher
