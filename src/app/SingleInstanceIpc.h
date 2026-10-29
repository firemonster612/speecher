#pragma once

#include "app/IpcMessages.h"
#include "app/PlatformComposition.h"

#include <QDeadlineTimer>
#include <QLocalServer>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>

#include <functional>
#include <memory>
#include <optional>

namespace speecher {

// How long a client waits for the running instance to take a request and
// to answer it.
inline constexpr int kIpcTimeoutMs = 2500;

enum class IpcCommandResult {
    Sent,
    Unavailable,
    NoResponse,
    InvalidResponse,
};

enum class IpcListenResult {
    Listening,
    // Another Speecher holds the socket; it answers requests in our place.
    AnotherInstance,
    Failed,
};

class SingleInstanceIpc : public QObject {
    Q_OBJECT

public:
    explicit SingleInstanceIpc(std::shared_ptr<const SingleInstancePlatform> platform = {}, QObject *parent = nullptr);
    ~SingleInstanceIpc() override;

    // error says why when the result is not Listening.
    IpcListenResult listen(QString *error = nullptr);
    QString socketName() const;
    static QString socketName(std::shared_ptr<const SingleInstancePlatform> platform);
    static IpcCommandResult sendRequest(const IpcRequest &request,
                                        IpcResponse *response,
                                        int timeoutMs = kIpcTimeoutMs,
                                        std::shared_ptr<const SingleInstancePlatform> platform = {},
                                        QString *error = nullptr);
    // The watchStatus command, for status --watch: calls onStatus with each
    // status the running instance writes, the current one at once and then
    // one on every change, until the instance closes the connection or
    // onStatus returns false. Sent once it has answered; Unavailable when
    // none is running, NoResponse when one closes the connection or stays
    // silent before answering.
    static IpcCommandResult watchStatus(const std::function<bool(const IpcResponse &)> &onStatus,
                                        int timeoutMs = kIpcTimeoutMs,
                                        std::shared_ptr<const SingleInstancePlatform> platform = {},
                                        QString *error = nullptr);

    // Keeps socket open as a status watcher: writes it status now and every
    // status publishStatus is given, until it disconnects.
    void addStatusWatcher(QLocalSocket *socket, const IpcResponse &status);
    void publishStatus(const IpcResponse &status);

signals:
    // Answer each with writeResponse.
    void requestReceived(const IpcRequest &request, QLocalSocket *socket);
    // A request the instance cannot serve, and the answer it gets once the
    // instance fills in its state.
    void requestRefused(const IpcResponse &refusal, QLocalSocket *socket);

public slots:
    static void writeResponse(QLocalSocket *socket, const IpcResponse &response);

private:
    std::shared_ptr<const SingleInstancePlatform> m_platform;
    QLocalServer m_server;
#ifdef Q_OS_WIN
    void *m_instanceGuard = nullptr;
#endif
    QHash<QLocalSocket *, QByteArray> m_requestBuffers;
    // Sockets currently inside their own command handling. A command can pump
    // the message loop (XAML islands on Windows do), so we hold off deleting a
    // disconnected socket until its handler returns rather than let the nested
    // pump free it underneath us.
    QSet<QLocalSocket *> m_socketsInCommand;
    QSet<QLocalSocket *> m_socketsPendingDelete;
    // Resource bounds: a broken or hostile local client must not hold sockets
    // and buffers open indefinitely. Accepted sockets are capped, and a socket
    // whose buffer holds an incomplete request past its deadline is expired by
    // the sweep timer.
    QSet<QLocalSocket *> m_acceptedSockets;
    QHash<QLocalSocket *, QDeadlineTimer> m_incompleteRequestDeadlines;
    QTimer m_expirySweep;
    // Watchers hold their socket open for as long as they watch, so they are
    // capped apart from the accepted sockets. Held by QPointer, so one that
    // goes away needs no bookkeeping.
    QList<QPointer<QLocalSocket>> m_statusWatchers;
};

} // namespace speecher
