#include "app/SingleInstanceIpc.h"

#include <QDeadlineTimer>
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

SingleInstanceIpc::SingleInstanceIpc(std::shared_ptr<const SingleInstancePlatform> platform, QObject *parent)
    : QObject(parent)
    , m_platform(platform ? std::move(platform) : linuxComposition())
{
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = m_server.nextPendingConnection()) {
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                // Collect complete frames before emitting: a commandReceived slot can
                // disconnect the socket, whose disconnected handler removes the buffer
                // this loop would otherwise still reference.
                QList<QByteArray> frames;
                {
                    QByteArray &buffer = m_requestBuffers[socket];
                    buffer.append(socket->readAll());
                    while (true) {
                        const qsizetype newline = buffer.indexOf('\n');
                        if (newline >= 0) {
                            frames.append(buffer.left(newline));
                            buffer.remove(0, newline + 1);
                            continue;
                        }
                        QJsonParseError legacyError;
                        const QJsonDocument legacy = QJsonDocument::fromJson(buffer, &legacyError);
                        if (legacyError.error == QJsonParseError::NoError && legacy.isObject()) {
                            frames.append(std::exchange(buffer, {}));
                        }
                        break;
                    }
                }
                for (const QByteArray &frame : frames) {
                    QJsonParseError parseError;
                    const QJsonDocument document = QJsonDocument::fromJson(frame, &parseError);
                    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
                        continue;
                    }
                    const QJsonObject object = document.object();
                    emit commandReceived(object.value(QStringLiteral("command")).toString(),
                                         object.value(QStringLiteral("outputFormat")).toString(),
                                         socket);
                }
            });
            connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
                m_requestBuffers.remove(socket);
                socket->deleteLater();
            });
        }
    });
}

SingleInstanceIpc::~SingleInstanceIpc()
{
    // Sever per-socket lambdas before member destruction: m_requestBuffers dies
    // before m_server, whose dying sockets would otherwise emit disconnected
    // into the already-destroyed hash.
    for (QLocalSocket *socket : m_server.findChildren<QLocalSocket *>()) {
        socket->disconnect(this);
    }
}

QString SingleInstanceIpc::socketName() const
{
    return m_platform->ipcListenName();
}

QString SingleInstanceIpc::socketName(std::shared_ptr<const SingleInstancePlatform> platform)
{
    const std::shared_ptr<const SingleInstancePlatform> resolved = platform ? std::move(platform) : linuxComposition();
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
                                    std::shared_ptr<const SingleInstancePlatform> platform)
{
    return sendCommandDetailed(command,
                               std::nullopt,
                               response,
                               timeoutMs,
                               std::move(platform),
                               nullptr) == IpcCommandResult::Sent;
}

IpcCommandResult SingleInstanceIpc::sendCommandDetailed(const QString &command,
                                                        IpcResponse *response,
                                                        int timeoutMs,
                                                        std::shared_ptr<const SingleInstancePlatform> platform,
                                                        QString *error)
{
    return sendCommandDetailed(command, std::nullopt, response, timeoutMs, std::move(platform), error);
}

IpcCommandResult SingleInstanceIpc::sendCommandDetailed(const QString &command,
                                                        std::optional<OutputFormat> outputFormat,
                                                        IpcResponse *response,
                                                        int timeoutMs,
                                                        std::shared_ptr<const SingleInstancePlatform> platform,
                                                        QString *error)
{
    const std::shared_ptr<const SingleInstancePlatform> resolved = platform ? std::move(platform) : linuxComposition();
    for (const QString &candidate : resolved->ipcConnectCandidates()) {
        QLocalSocket socket;
        socket.connectToServer(candidate);
        if (!socket.waitForConnected(timeoutMs)) {
            continue;
        }
        QJsonObject request{{QStringLiteral("command"), command}};
        if (outputFormat) {
            request.insert(QStringLiteral("outputFormat"), outputFormatName(*outputFormat));
        }
        QByteArray requestBytes = QJsonDocument(request).toJson(QJsonDocument::Compact);
        requestBytes.append('\n');
        if (socket.write(requestBytes) != requestBytes.size()) {
            if (error) {
                *error = QStringLiteral("Could not write command to running Speecher instance");
            }
            return IpcCommandResult::NoResponse;
        }
        socket.flush();
        QDeadlineTimer deadline(timeoutMs);
        QByteArray responseBytes;
        while (!responseBytes.contains('\n') && deadline.remainingTime() > 0) {
            if (socket.bytesAvailable() == 0
                && !socket.waitForReadyRead(deadline.remainingTime())) {
                break;
            }
            responseBytes.append(socket.readAll());
        }
        if (responseBytes.isEmpty()) {
            if (error) {
                *error = QStringLiteral("Running Speecher instance did not respond");
            }
            return IpcCommandResult::NoResponse;
        }
        QJsonParseError parseError;
        const qsizetype newline = responseBytes.indexOf('\n');
        const QByteArray frame = newline >= 0 ? responseBytes.left(newline) : responseBytes;
        const QJsonDocument document = QJsonDocument::fromJson(frame, &parseError);
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
    QByteArray responseBytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    responseBytes.append('\n');
    socket->write(responseBytes);
    socket->flush();
    socket->disconnectFromServer();
}

} // namespace speecher
