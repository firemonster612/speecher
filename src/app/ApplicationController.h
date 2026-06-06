#pragma once

#include <memory>

#include <QObject>

#include "app/SingleInstanceIpc.h"

class QLocalSocket;

namespace speecher {

class DictationSession;
class MainWindow;
class PlatformIntegration;
class ProviderRegistry;
class SecretStore;
class SettingsStore;
class TranscriberPopup;

class ApplicationController : public QObject {
    Q_OBJECT

public:
    explicit ApplicationController(bool popupOnly, QObject *parent = nullptr);

    SettingsStore *settings() const;
    SecretStore *secretStore() const;
    ProviderRegistry *providerRegistry() const;
    const PlatformIntegration *platform() const;
    QString stateName() const;
    IpcResponse response(bool ok = true, const QString &message = {}) const;
    QString outputSummary() const;
    QString primaryOutputStatus() const;

    void showMainWindow();
    bool startIpc(QString *error = nullptr);

public slots:
    void toggle();
    void startListening();
    void stopListening();
    void showMain();
    void handleIpcCommand(const QString &command, QLocalSocket *socket);

signals:
    void statusChanged(const QString &status);
    void previewChanged(const QString &preview);

private:
    void registerProviders();
    void wireSessionToPopup();

    bool m_popupOnly = false;
    std::shared_ptr<const PlatformIntegration> m_platform;
    SettingsStore *m_settings = nullptr;
    SecretStore *m_secrets = nullptr;
    ProviderRegistry *m_providers = nullptr;
    DictationSession *m_session = nullptr;
    TranscriberPopup *m_popup = nullptr;
    MainWindow *m_mainWindow = nullptr;
    SingleInstanceIpc *m_ipc = nullptr;
};

} // namespace speecher
