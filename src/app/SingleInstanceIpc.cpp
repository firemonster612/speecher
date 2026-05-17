#include "app/SingleInstanceIpc.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>

#include <unistd.h>

namespace speecher {

SingleInstanceIpc::SingleInstanceIpc(QObject *parent)
    : QObject(parent)
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

QString SingleInstanceIpc::socketName()
{
    if (!qgetenv("APPIMAGE").isEmpty()) {
        return QStringLiteral("speecher-%1-appimage").arg(getuid());
    }

    const QFileInfo executable(QCoreApplication::applicationFilePath());
    QString path = executable.canonicalFilePath();
    if (path.isEmpty()) {
        path = executable.absoluteFilePath();
    }
    const QByteArray digest = QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex().left(12);
    return QStringLiteral("speecher-%1-%2").arg(getuid()).arg(QString::fromLatin1(digest));
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

bool SingleInstanceIpc::sendCommand(const QString &command, IpcResponse *response, int timeoutMs)
{
    QLocalSocket socket;
    socket.connectToServer(socketName());
    if (!socket.waitForConnected(timeoutMs)) {
        return false;
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
