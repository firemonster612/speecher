#pragma once

#include <memory>

#include <QObject>

#include "app/SingleInstanceIpc.h"

class QLocalSocket;

namespace speecher {

class DictationSession;
class AppWindow;
class LinuxComposition;
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
    const LinuxComposition *platform() const;
    QString stateName() const;
    IpcResponse response(bool ok = true, const QString &message = {}) const;
    QString outputSummary() const;
    QString primaryOutputStatus() const;
    bool accessibilitySupported() const;
    bool accessibilityEnabled() const;
    bool accessibilityPersistent() const;
    bool enableAccessibility(QString *error = nullptr);
    bool grabMainWindow(const QString &path) const;

    void showMainWindow();
    void showSettingsWindow();
    bool startIpc(QString *error = nullptr);

public slots:
    void toggle();
    void startListening();
    void stopListening();
    void showMain();
    void showSettings();
    void handleIpcCommand(const QString &command,
                          const QString &outputFormat,
                          QLocalSocket *socket);

signals:
    void statusChanged(const QString &status);
    void previewChanged(const QString &preview);
    void audioLevelChanged(float level);
    void accessibilityStateChanged(bool supported, bool enabled, bool persistent);

private:
    void registerProviders();
    void wireSessionToPopup();
    void refreshAccessibilityState();

    bool m_popupOnly = false;
    std::shared_ptr<const LinuxComposition> m_platform;
    SettingsStore *m_settings = nullptr;
    SecretStore *m_secrets = nullptr;
    ProviderRegistry *m_providers = nullptr;
    DictationSession *m_session = nullptr;
    TranscriberPopup *m_popup = nullptr;
    AppWindow *m_appWindow = nullptr;
    SingleInstanceIpc *m_ipc = nullptr;
    bool m_accessibilitySupported = false;
    bool m_accessibilityEnabled = false;
    bool m_accessibilityPersistent = false;
};

} // namespace speecher
