#pragma once

#include "app/PlatformComposition.h"
#include "dictation/DictationTypes.h"

#include <QDeadlineTimer>
#include <QLocalServer>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QTimer>

#include <memory>
#include <optional>

namespace speecher {

struct IpcResponse {
    bool ok = false;
    QString state;
    QString message;
    // The Writing Profile id and Spoken Language a toggle or start request
    // carried, echoed so a client can tell an instance that predates
    // --profile or --language, which ignores them.
    QString writingProfile;
    QString spokenLanguage;
};

enum class IpcCommandResult {
    Sent,
    Unavailable,
    NoResponse,
    InvalidResponse,
};

class SingleInstanceIpc : public QObject {
    Q_OBJECT

public:
    explicit SingleInstanceIpc(std::shared_ptr<const SingleInstancePlatform> platform = {}, QObject *parent = nullptr);
    ~SingleInstanceIpc() override;

    bool listen(QString *error = nullptr);
    QString socketName() const;
    static QString socketName(std::shared_ptr<const SingleInstancePlatform> platform);
    static bool sendCommand(const QString &command,
                            IpcResponse *response,
                            int timeoutMs = 2500,
                            std::shared_ptr<const SingleInstancePlatform> platform = {});
    static IpcCommandResult sendCommandDetailed(const QString &command,
                                                IpcResponse *response,
                                                int timeoutMs = 2500,
                                                std::shared_ptr<const SingleInstancePlatform> platform = {},
                                                QString *error = nullptr);
    static IpcCommandResult sendCommandDetailed(const QString &command,
                                                const SessionOverrides &overrides,
                                                IpcResponse *response,
                                                int timeoutMs = 2500,
                                                std::shared_ptr<const SingleInstancePlatform> platform = {},
                                                QString *error = nullptr);
    // files rides along as the request's "files" array (the transcribe command).
    static IpcCommandResult sendCommandDetailed(const QString &command,
                                                const SessionOverrides &overrides,
                                                const QStringList &files,
                                                IpcResponse *response,
                                                int timeoutMs = 2500,
                                                std::shared_ptr<const SingleInstancePlatform> platform = {},
                                                QString *error = nullptr);

signals:
    void commandReceived(const QString &command,
                         const QString &outputFormat,
                         QLocalSocket *socket,
                         const QStringList &files,
                         const QString &writingProfile,
                         const QString &spokenLanguage);

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
};

} // namespace speecher
