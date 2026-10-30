#include "app/SingleInstanceIpc.h"

#include <QDeadlineTimer>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLocalSocket>
#ifdef Q_OS_WIN
#include <QCryptographicHash>
#include <QScopeGuard>
#include <qt_windows.h>
#endif

#include <algorithm>
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
constexpr int maximumStatusWatchers = 32;
// A watcher that stops reading is dropped once this much waits for it,
// rather than buffered for without end.
constexpr qint64 maximumStatusBacklogBytes = 256 * 1024;

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

// Connects socket to the running instance and writes request to it.
// Unavailable when no instance accepts the connection.
IpcCommandResult connectAndWrite(QLocalSocket &socket,
                                 const IpcRequest &request,
                                 int timeoutMs,
                                 std::shared_ptr<const SingleInstancePlatform> platform,
                                 QString *error)
{
    const std::shared_ptr<const SingleInstancePlatform> resolved = platform ? std::move(platform) : platformComposition();
    for (const QString &candidate : resolved->ipcConnectCandidates()) {
        socket.connectToServer(candidate);
        if (!socket.waitForConnected(timeoutMs)) {
            socket.abort();
            continue;
        }
        const QByteArray requestBytes = ipcRequestFrame(request);
        if (socket.write(requestBytes) != requestBytes.size()) {
            if (error) {
                *error = QStringLiteral("Could not write command to running Speecher instance");
            }
            return IpcCommandResult::NoResponse;
        }
        socket.flush();
        return IpcCommandResult::Sent;
    }
    return IpcCommandResult::Unavailable;
}

QString invalidResponseMessage()
{
    return QStringLiteral("Running Speecher instance returned an invalid IPC response");
}

QString noResponseMessage()
{
    return QStringLiteral("Running Speecher instance did not respond");
}

// Writes a status watcher its next line without waiting on it: one that has
// left more than the backlog unread is dropped.
void writeStatus(QLocalSocket *socket, const QByteArray &frame)
{
    if (socket->state() != QLocalSocket::ConnectedState) {
        return;
    }
    if (socket->bytesToWrite() + frame.size() > maximumStatusBacklogBytes) {
        socket->abort();
        return;
    }
    socket->write(frame);
    socket->flush();
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
                // A watcher has asked all it may: another watchStatus would
                // write it every line twice, and any other command's answer
                // would end its watch.
                if (m_statusWatchers.contains(socket)) {
                    socket->readAll();
                    return;
                }
                // Collect complete frames before emitting: a requestReceived slot can
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
                    // Blank lines are not a request, so they keep the
                    // deadline: a client sending only those would otherwise
                    // hold an accept slot forever.
                    const bool requestLanded = std::ranges::any_of(
                        frames, [](const QByteArray &frame) { return !frame.trimmed().isEmpty(); });
                    if (buffer.isEmpty()) {
                        if (requestLanded) {
                            m_incompleteRequestDeadlines.remove(socket);
                        }
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
                    if (m_statusWatchers.contains(socket)) {
                        break;
                    }
                    // A newline sent after a request already read without one
                    // ends that request; it is not a request of its own.
                    if (frame.trimmed().isEmpty()) {
                        continue;
                    }
                    IpcResponse refusal;
                    if (const std::optional<IpcRequest> request = ipcRequestFromFrame(frame, &refusal)) {
                        emit requestReceived(*request, socket);
                    } else {
                        emit requestRefused(refusal, socket);
                    }
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

IpcListenResult SingleInstanceIpc::listen(QString *error)
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
        return guard ? IpcListenResult::AnotherInstance : IpcListenResult::Failed;
    }
    if (canConnectToServer(listenName, 200)) {
        if (error) {
            *error = activeInstanceMessage(listenName);
        }
        return IpcListenResult::AnotherInstance;
    }
#endif
    for (const QString &candidate : m_platform->ipcConnectCandidates()) {
        if (candidate != listenName && canConnectToServer(candidate, 200)) {
            if (error) {
                *error = activeInstanceMessage(candidate);
            }
            return IpcListenResult::AnotherInstance;
        }
    }

    if (m_server.listen(listenName)) {
#ifdef Q_OS_WIN
        m_instanceGuard = std::exchange(guard, nullptr);
#endif
        return IpcListenResult::Listening;
    }

    const QString firstError = m_server.errorString();
    m_server.close();
    if (canConnectToServer(listenName, 200)) {
        if (error) {
            *error = activeInstanceMessage(listenName);
        }
        return IpcListenResult::AnotherInstance;
    }

    QLocalServer::removeServer(listenName);
    if (!m_server.listen(listenName)) {
        if (error) {
            *error = m_server.errorString().isEmpty() ? firstError : m_server.errorString();
        }
        return IpcListenResult::Failed;
    }
#ifdef Q_OS_WIN
    m_instanceGuard = std::exchange(guard, nullptr);
#endif
    return IpcListenResult::Listening;
}

IpcCommandResult SingleInstanceIpc::sendRequest(const IpcRequest &request,
                                                IpcResponse *response,
                                                int timeoutMs,
                                                std::shared_ptr<const SingleInstancePlatform> platform,
                                                QString *error)
{
    QLocalSocket socket;
    const IpcCommandResult connected = connectAndWrite(socket, request, timeoutMs, std::move(platform), error);
    if (connected != IpcCommandResult::Sent) {
        return connected;
    }
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
            *error = noResponseMessage();
        }
        return IpcCommandResult::NoResponse;
    }
    const qsizetype newline = responseBytes.indexOf('\n');
    const std::optional<IpcResponse> answer =
        ipcResponseFromFrame(newline >= 0 ? responseBytes.left(newline) : responseBytes);
    if (!answer) {
        if (error) {
            *error = invalidResponseMessage();
        }
        return IpcCommandResult::InvalidResponse;
    }
    if (response) {
        *response = *answer;
    }
    return IpcCommandResult::Sent;
}

IpcCommandResult SingleInstanceIpc::watchStatus(const std::function<bool(const IpcResponse &)> &onStatus,
                                                int timeoutMs,
                                                std::shared_ptr<const SingleInstancePlatform> platform,
                                                QString *error)
{
    QLocalSocket socket;
    const IpcCommandResult connected = connectAndWrite(
        socket, {IpcCommand::WatchStatus}, timeoutMs, std::move(platform), error);
    if (connected != IpcCommandResult::Sent) {
        return connected;
    }
    // The first status is due at once. The next comes whenever something
    // changes, so it is waited for as long as the instance runs.
    QByteArray buffer;
    bool answered = false;
    bool open = true;
    while (open) {
        open = socket.waitForReadyRead(answered ? -1 : timeoutMs);
        buffer.append(socket.readAll());
        for (qsizetype newline = buffer.indexOf('\n'); newline >= 0; newline = buffer.indexOf('\n')) {
            const std::optional<IpcResponse> status = ipcResponseFromFrame(buffer.left(newline));
            if (!status) {
                if (error) {
                    *error = invalidResponseMessage();
                }
                return IpcCommandResult::InvalidResponse;
            }
            buffer.remove(0, newline + 1);
            answered = true;
            if (!onStatus(*status)) {
                return IpcCommandResult::Sent;
            }
        }
    }
    if (!answered) {
        if (error) {
            *error = noResponseMessage();
        }
        return IpcCommandResult::NoResponse;
    }
    return IpcCommandResult::Sent;
}

void SingleInstanceIpc::addStatusWatcher(QLocalSocket *socket, const IpcResponse &status)
{
    if (!socket || socket->state() != QLocalSocket::ConnectedState) {
        return;
    }
    m_statusWatchers.removeAll(nullptr);
    if (m_statusWatchers.size() >= maximumStatusWatchers) {
        writeResponse(socket, {false, status.state, QStringLiteral("Too many status watchers are connected")});
        return;
    }
    // A watcher has had its answer, so it no longer holds an accept slot,
    // which every other command needs. Nor is anything it sends after
    // watchStatus a request, so a partial one must not expire it.
    m_acceptedSockets.remove(socket);
    m_requestBuffers.remove(socket);
    m_incompleteRequestDeadlines.remove(socket);
    m_statusWatchers.append(socket);
    writeStatus(socket, ipcResponseFrame(status));
}

void SingleInstanceIpc::publishStatus(const IpcResponse &status)
{
    const QByteArray frame = ipcResponseFrame(status);
    // Copied: dropping a watcher runs its disconnected handler, which can
    // delete it and null its entry.
    for (const QPointer<QLocalSocket> &socket : QList(m_statusWatchers)) {
        if (socket) {
            writeStatus(socket, frame);
        }
    }
}

void SingleInstanceIpc::writeResponse(QLocalSocket *socket, const IpcResponse &response)
{
    // A client that disconnects right after sending can deliver its command
    // from inside the socket's own dying state change; writing the response
    // into that teardown crashes inside QIODevice::write on Windows.
    if (!socket || socket->state() != QLocalSocket::ConnectedState) {
        return;
    }
    socket->write(ipcResponseFrame(response));
    socket->flush();
    socket->disconnectFromServer();
}

} // namespace speecher
