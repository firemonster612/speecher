#include "app/SingleInstanceIpc.h"

#include "platform/PlatformIntegration.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>

#include <utility>

namespace speecher {

SingleInstanceIpc::SingleInstanceIpc(std::shared_ptr<const PlatformIntegration> platform, QObject *parent)
    : QObject(parent)
    , m_platform(platform ? std::move(platform) : PlatformFactory::create())
{
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = m_server.nextPendingConnection()) {
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                const QJsonObject object = QJsonDocument::fromJson(socket->readAll()).object();
                emit commandReceived(object.value(QStringLiteral("command")).toString(), socket);
            });
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
}

QString SingleInstanceIpc::socketName() const
{
    return m_platform->ipcListenName();
}

QString SingleInstanceIpc::socketName(std::shared_ptr<const PlatformIntegration> platform)
{
    const std::shared_ptr<const PlatformIntegration> resolved = platform ? std::move(platform) : PlatformFactory::create();
    return resolved->ipcListenName();
}

bool SingleInstanceIpc::listen(QString *error)
{
    QLocalServer::removeServer(socketName());
    if (!m_server.listen(socketName())) {
        if (error) {
            *error = m_server.errorString();
        }
        return false;
    }
    return true;
}

bool SingleInstanceIpc::sendCommand(const QString &command,
                                    IpcResponse *response,
                                    int timeoutMs,
                                    std::shared_ptr<const PlatformIntegration> platform)
{
    const std::shared_ptr<const PlatformIntegration> resolved = platform ? std::move(platform) : PlatformFactory::create();
    for (const QString &candidate : resolved->ipcConnectCandidates()) {
        QLocalSocket socket;
        socket.connectToServer(candidate);
        if (!socket.waitForConnected(timeoutMs)) {
            continue;
        }
        const QJsonObject request{{QStringLiteral("command"), command}};
        socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact));
        socket.flush();
        if (!socket.waitForReadyRead(timeoutMs)) {
            return false;
        }
        const QJsonObject object = QJsonDocument::fromJson(socket.readAll()).object();
        if (response) {
            response->ok = object.value(QStringLiteral("ok")).toBool();
            response->state = object.value(QStringLiteral("state")).toString();
            response->message = object.value(QStringLiteral("message")).toString();
        }
        return true;
    }
    return false;
}

void SingleInstanceIpc::writeResponse(QLocalSocket *socket, const IpcResponse &response)
{
    if (!socket) {
        return;
    }
    const QJsonObject object{
        {QStringLiteral("ok"), response.ok},
        {QStringLiteral("state"), response.state},
        {QStringLiteral("message"), response.message.isEmpty() ? QJsonValue() : QJsonValue(response.message)},
    };
    socket->write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    socket->flush();
    socket->disconnectFromServer();
}

} // namespace speecher
