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
class AudioInput;
class AppWindow;
class ProviderRegistry;
class SecretStore;
class SettingsStore;
class SetupAssistant;
class TranscriberPopup;

class ApplicationController : public QObject {
    Q_OBJECT

public:
    explicit ApplicationController(bool popupOnly,
                                   std::shared_ptr<const PlatformComposition> platform = platformComposition(),
                                   QObject *parent = nullptr);
    ~ApplicationController() override;

    SettingsStore *settings() const;
    SecretStore *secretStore() const;
    ProviderRegistry *providerRegistry() const;
    const PlatformComposition *platform() const;
    QString stateName() const;
    IpcResponse response(bool ok = true, const QString &message = {}) const;
    QString outputSummary() const;
    QString primaryOutputStatus() const;
    bool accessibilitySupported() const;
    bool accessibilityEnabled() const;
    bool accessibilityPersistent() const;
    bool enableAccessibility(QString *error = nullptr);
    bool grabMainWindow(const QString &path) const;
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
    void handleIpcCommand(const QString &command,
                          const QString &outputFormat,
                          QLocalSocket *socket);

signals:
    void stateChanged(const QString &stateName);
    void statusChanged(const QString &status);
    void previewChanged(const QString &preview);
    void transcriptDelivered(const QString &text);
    void audioLevelChanged(float level);
    void accessibilityStateChanged(bool supported, bool enabled, bool persistent);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void registerProviders();
    void wireSessionToPopup();
    void refreshAccessibilityState();
    void runDeferredStartup();
    bool ensureSetupCompleted();

    bool m_popupOnly = false;
    std::shared_ptr<const PlatformComposition> m_platform;
    SettingsStore *m_settings = nullptr;
    SecretStore *m_secrets = nullptr;
    ProviderRegistry *m_providers = nullptr;
    AudioInput *m_audio = nullptr;
    DictationSession *m_session = nullptr;
    TranscriberPopup *m_popup = nullptr;
    AppWindow *m_appWindow = nullptr;
    QPointer<SetupAssistant> m_setupAssistant;
    QAction *m_globalShortcutAction = nullptr;
    SingleInstanceIpc *m_ipc = nullptr;
    bool m_accessibilitySupported = false;
    bool m_accessibilityEnabled = false;
    bool m_accessibilityPersistent = false;
    bool m_deferredStartupScheduled = false;
    bool m_deferredStartupDone = false;
};

} // namespace speecher
