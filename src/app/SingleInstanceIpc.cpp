#include "app/SingleInstanceIpc.h"

#include <QDeadlineTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLocalSocket>
#ifdef Q_OS_WIN
#include <QCryptographicHash>
#include <QScopeGuard>
#include <qt_windows.h>
#endif

#include <utility>

namespace speecher {

namespace {

constexpr qsizetype maximumRequestBytes = 64 * 1024;
// Legitimate clients connect, write one frame, and wait for the response, so
// these bounds never bite them; they stop a broken or hostile local client
// from holding descriptors and buffers open indefinitely.
constexpr int maximumAcceptedSockets = 8;
constexpr int incompleteRequestTimeoutMs = 2000;
constexpr int expirySweepIntervalMs = 500;

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
    , m_platform(platform ? std::move(platform) : platformComposition())
{
    m_expirySweep.setInterval(expirySweepIntervalMs);
    connect(&m_expirySweep, &QTimer::timeout, this, [this] {
        // Keys are copied: disconnecting re-enters the disconnected handler,
        // which mutates the hash.
        const QList<QLocalSocket *> sockets = m_incompleteRequestDeadlines.keys();
        for (QLocalSocket *socket : sockets) {
            if (m_incompleteRequestDeadlines.value(socket).hasExpired()) {
                socket->disconnectFromServer();
            }
        }
        if (m_incompleteRequestDeadlines.isEmpty()) {
            m_expirySweep.stop();
        }
    });
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = m_server.nextPendingConnection()) {
            if (m_acceptedSockets.size() >= maximumAcceptedSockets) {
                socket->disconnectFromServer();
                socket->deleteLater();
                continue;
            }
            m_acceptedSockets.insert(socket);
            // Armed at accept, not at first byte: a client that connects and
            // never writes would otherwise never enter the expiry map and
            // hold one of the accept slots forever — eight of those would
            // lock every later client out. The complete-frame branch below
            // removes the deadline once a request lands.
            m_incompleteRequestDeadlines.insert(
                socket, QDeadlineTimer(incompleteRequestTimeoutMs));
            m_expirySweep.start();
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                // Collect complete frames before emitting: a commandReceived slot can
                // disconnect the socket, whose disconnected handler removes the buffer
                // this loop would otherwise still reference.
                QList<QByteArray> frames;
                bool requestTooLarge = false;
                {
                    QByteArray &buffer = m_requestBuffers[socket];
                    buffer.append(socket->readAll());
                    requestTooLarge = buffer.size() > maximumRequestBytes;
                    while (!requestTooLarge) {
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
                    if (buffer.isEmpty()) {
                        m_incompleteRequestDeadlines.remove(socket);
                    } else if (!m_incompleteRequestDeadlines.contains(socket)) {
                        // The deadline dates from accept (or from the first
                        // incomplete byte after a completed request);
                        // trickling more bytes in does not extend it.
                        m_incompleteRequestDeadlines.insert(
                            socket, QDeadlineTimer(incompleteRequestTimeoutMs));
                        m_expirySweep.start();
                    }
                }
                if (requestTooLarge) {
                    socket->disconnectFromServer();
                    return;
                }
                // A command handler can pump the message loop (XAML islands do
                // on Windows) and process a deleteLater queued by this socket's
                // own disconnect, freeing it while we still hold the pointer.
                // Hold deletion off until every frame is handled.
                m_socketsInCommand.insert(socket);
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
                m_socketsInCommand.remove(socket);
                if (m_socketsPendingDelete.remove(socket)) {
                    m_requestBuffers.remove(socket);
                    m_incompleteRequestDeadlines.remove(socket);
                    m_acceptedSockets.remove(socket);
                    socket->deleteLater();
                }
            });
            connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
                // Deferred while a command from this socket is in flight; the
                // readyRead handler deletes it once its loop unwinds.
                if (m_socketsInCommand.contains(socket)) {
                    m_socketsPendingDelete.insert(socket);
                    return;
                }
                m_requestBuffers.remove(socket);
                m_incompleteRequestDeadlines.remove(socket);
                m_acceptedSockets.remove(socket);
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
#ifdef Q_OS_WIN
    m_server.close();
    if (m_instanceGuard) {
        CloseHandle(m_instanceGuard);
    }
#endif
}

QString SingleInstanceIpc::socketName() const
{
    return m_platform->ipcListenName();
}

QString SingleInstanceIpc::socketName(std::shared_ptr<const SingleInstancePlatform> platform)
{
    const std::shared_ptr<const SingleInstancePlatform> resolved = platform ? std::move(platform) : platformComposition();
    return resolved->ipcListenName();
}

bool SingleInstanceIpc::listen(QString *error)
{
    const QString listenName = socketName();
#ifdef Q_OS_WIN
    // Windows permits multiple QLocalServers on one pipe. Keep an atomic
    // process-lifetime claim while probing legacy endpoints and opening ours.
    const QString guardName = QStringLiteral("Global\\speecher-instance-")
        + QString::fromLatin1(QCryptographicHash::hash(listenName.toUtf8(),
                                                      QCryptographicHash::Sha256).toHex());
    HANDLE guard = CreateMutexW(nullptr, FALSE, guardName.toStdWString().c_str());
    const DWORD guardError = GetLastError();
    const auto releaseGuard = qScopeGuard([&] {
        if (guard) {
            CloseHandle(guard);
        }
    });
    if (!guard || guardError == ERROR_ALREADY_EXISTS) {
        if (error) {
            *error = guard ? activeInstanceMessage(listenName)
                           : QStringLiteral("Could not reserve Speecher instance: Windows error %1")
                                 .arg(guardError);
        }
        return false;
    }
    if (canConnectToServer(listenName, 200)) {
        if (error) {
            *error = activeInstanceMessage(listenName);
        }
        return false;
    }
#endif
    for (const QString &candidate : m_platform->ipcConnectCandidates()) {
        if (candidate != listenName && canConnectToServer(candidate, 200)) {
            if (error) {
                *error = activeInstanceMessage(candidate);
            }
            return false;
        }
    }

    if (m_server.listen(listenName)) {
#ifdef Q_OS_WIN
        m_instanceGuard = std::exchange(guard, nullptr);
#endif
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
#ifdef Q_OS_WIN
    m_instanceGuard = std::exchange(guard, nullptr);
#endif
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
    const std::shared_ptr<const SingleInstancePlatform> resolved = platform ? std::move(platform) : platformComposition();
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
    // A client that disconnects right after sending can deliver its command
    // from inside the socket's own dying state change; writing the response
    // into that teardown crashes inside QIODevice::write on Windows.
    if (!socket || socket->state() != QLocalSocket::ConnectedState) {
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
