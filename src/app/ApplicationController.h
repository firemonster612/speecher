#pragma once

#include <memory>

#include <QObject>
#include <QPointer>
#include <QKeySequence>

#include "app/SingleInstanceIpc.h"

class QLocalSocket;
class QAction;

namespace speecher {

class DictationSession;
class MainWindow;
class LinuxComposition;
class ProviderRegistry;
class SecretStore;
class SettingsDialog;
class SettingsStore;
class SetupAssistant;
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
    bool globalShortcutsSupported() const;
    QKeySequence globalShortcut() const;
    bool setGlobalShortcut(const QKeySequence &shortcut, QString *error = nullptr);

    void showMainWindow();
    void showSettingsWindow();
    void showSetupAssistant();
    bool startIpc(QString *error = nullptr);

public slots:
    void toggle();
    void startListening();
    void stopListening();
    void showMain();
    void showSettings();
    void showSetup();
    void handleIpcCommand(const QString &command, const QString &outputFormat, QLocalSocket *socket);

signals:
    void statusChanged(const QString &status);
    void previewChanged(const QString &preview);
    void accessibilityStateChanged(bool supported, bool enabled, bool persistent);

private:
    void registerProviders();
    void wireSessionToPopup();
    void refreshAccessibilityState();
    bool ensureSetupCompleted();

    bool m_popupOnly = false;
    std::shared_ptr<const LinuxComposition> m_platform;
    SettingsStore *m_settings = nullptr;
    SecretStore *m_secrets = nullptr;
    ProviderRegistry *m_providers = nullptr;
    DictationSession *m_session = nullptr;
    TranscriberPopup *m_popup = nullptr;
    MainWindow *m_mainWindow = nullptr;
    QPointer<SettingsDialog> m_settingsDialog;
    QPointer<SetupAssistant> m_setupAssistant;
    QAction *m_globalShortcutAction = nullptr;
    SingleInstanceIpc *m_ipc = nullptr;
    bool m_accessibilitySupported = false;
    bool m_accessibilityEnabled = false;
    bool m_accessibilityPersistent = false;
};

} // namespace speecher
