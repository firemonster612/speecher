#pragma once

#include <QLocalServer>
#include <QObject>

#include <memory>

namespace speecher {

class PlatformIntegration;

struct IpcResponse {
    bool ok = false;
    QString state;
    QString message;
};

class SingleInstanceIpc : public QObject {
    Q_OBJECT

public:
    explicit SingleInstanceIpc(std::shared_ptr<const PlatformIntegration> platform = {}, QObject *parent = nullptr);

    bool listen(QString *error = nullptr);
    QString socketName() const;
    static QString socketName(std::shared_ptr<const PlatformIntegration> platform);
    static bool sendCommand(const QString &command,
                            IpcResponse *response,
                            int timeoutMs = 1200,
                            std::shared_ptr<const PlatformIntegration> platform = {});

signals:
    void commandReceived(const QString &command, QLocalSocket *socket);

public slots:
    static void writeResponse(QLocalSocket *socket, const IpcResponse &response);

private:
    std::shared_ptr<const PlatformIntegration> m_platform;
    QLocalServer m_server;
};

} // namespace speecher
