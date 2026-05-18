#include "app/SingleInstanceIpc.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>

#include <unistd.h>

namespace speecher {

namespace {

QString appSocketName()
{
    return QStringLiteral("speecher-%1").arg(getuid());
}

QString appImageSocketName()
{
    return QStringLiteral("speecher-%1-appimage").arg(getuid());
}

bool isRunningFromOwnAppImage()
{
    const QString appDir = QString::fromLocal8Bit(qgetenv("APPDIR"));
    if (qgetenv("APPIMAGE").isEmpty() || appDir.isEmpty()) {
        return false;
    }

    const QString executablePath = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
    const QString appDirPath = QFileInfo(appDir).canonicalFilePath();
    return !executablePath.isEmpty()
        && !appDirPath.isEmpty()
        && executablePath.startsWith(appDirPath + QDir::separator());
}

QString executablePathSocketName()
{
    const QFileInfo executable(QCoreApplication::applicationFilePath());
    QString path = executable.canonicalFilePath();
    if (path.isEmpty()) {
        path = executable.absoluteFilePath();
    }
    const QByteArray digest = QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex().left(12);
    return QStringLiteral("speecher-%1-%2").arg(getuid()).arg(QString::fromLatin1(digest));
}

QStringList socketCandidates()
{
    if (isRunningFromOwnAppImage()) {
        return {appImageSocketName()};
    }
    return {appSocketName(), executablePathSocketName()};
}

} // namespace

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
    if (isRunningFromOwnAppImage()) {
        return appImageSocketName();
    }
    return appSocketName();
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
    for (const QString &candidate : socketCandidates()) {
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
