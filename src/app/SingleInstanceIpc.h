#pragma once

#include <QLocalServer>
#include <QObject>

namespace speecher {

struct IpcResponse {
    bool ok = false;
    QString state;
    QString message;
};

class SingleInstanceIpc : public QObject {
    Q_OBJECT

public:
    explicit SingleInstanceIpc(QObject *parent = nullptr);

    bool listen(QString *error = nullptr);
    static QString socketName();
    static bool sendCommand(const QString &command, IpcResponse *response, int timeoutMs = 1200);

signals:
    void commandReceived(const QString &command, QLocalSocket *socket);

public slots:
    static void writeResponse(QLocalSocket *socket, const IpcResponse &response);

private:
    QLocalServer m_server;
};

} // namespace speecher
