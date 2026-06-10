#include "app/SingleInstanceIpc.h"

#include "platform/PlatformIntegration.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLocalSocket>

#include <utility>

namespace speecher {

namespace {

bool canConnectToServer(const QString &name, int timeoutMs)
{
    QLocalSocket socket;
    socket.connectToServer(name);
    if (!socket.waitForConnected(timeoutMs)) {
        return false;
    }
    socket.disconnectFromServer();
    if (socket.state() != QLocalSocket::UnconnectedState) {
        socket.waitForDisconnected(timeoutMs);
    }
    return true;
}

QString activeInstanceMessage(const QString &name)
{
    return QStringLiteral("Another Speecher instance is already running on %1").arg(name);
}

} // namespace

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
    const QString listenName = socketName();
    for (const QString &candidate : m_platform->ipcConnectCandidates()) {
        if (candidate != listenName && canConnectToServer(candidate, 200)) {
            if (error) {
                *error = activeInstanceMessage(candidate);
            }
            return false;
        }
    }

    if (m_server.listen(listenName)) {
        return true;
    }

    const QString firstError = m_server.errorString();
    m_server.close();
    if (canConnectToServer(listenName, 200)) {
        if (error) {
            *error = activeInstanceMessage(listenName);
        }
        return false;
    }

    QLocalServer::removeServer(listenName);
    if (!m_server.listen(listenName)) {
        if (error) {
            *error = m_server.errorString().isEmpty() ? firstError : m_server.errorString();
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
    return sendCommandDetailed(command, response, timeoutMs, std::move(platform)) == IpcCommandResult::Sent;
}

IpcCommandResult SingleInstanceIpc::sendCommandDetailed(const QString &command,
                                                        IpcResponse *response,
                                                        int timeoutMs,
                                                        std::shared_ptr<const PlatformIntegration> platform,
                                                        QString *error)
{
    const std::shared_ptr<const PlatformIntegration> resolved = platform ? std::move(platform) : PlatformFactory::create();
    for (const QString &candidate : resolved->ipcConnectCandidates()) {
        QLocalSocket socket;
        socket.connectToServer(candidate);
        if (!socket.waitForConnected(timeoutMs)) {
            continue;
        }
        const QJsonObject request{{QStringLiteral("command"), command}};
        if (socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact)) < 0) {
            if (error) {
                *error = QStringLiteral("Could not write command to running Speecher instance");
            }
            return IpcCommandResult::NoResponse;
        }
        socket.flush();
        if (!socket.waitForReadyRead(timeoutMs)) {
            if (error) {
                *error = QStringLiteral("Running Speecher instance did not respond");
            }
            return IpcCommandResult::NoResponse;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(socket.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            if (error) {
                *error = QStringLiteral("Running Speecher instance returned an invalid IPC response");
            }
            return IpcCommandResult::InvalidResponse;
        }
        const QJsonObject object = document.object();
        if (response) {
            response->ok = object.value(QStringLiteral("ok")).toBool();
            response->state = object.value(QStringLiteral("state")).toString();
            response->message = object.value(QStringLiteral("message")).toString();
        }
        return IpcCommandResult::Sent;
    }
    return IpcCommandResult::Unavailable;
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
