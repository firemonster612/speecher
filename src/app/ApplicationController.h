#pragma once

#include <functional>
#include <memory>

#include <QElapsedTimer>
#include <QObject>
#include <QKeySequence>

#include "app/SingleInstanceIpc.h"

class QLocalSocket;
class QTimer;

namespace speecher {

class AppFrontEnd;
enum class SetupAssistantPage;
class DictationSession;
class AudioInput;
class GlobalShortcutBinder;
class ProviderRegistry;
class SecretStore;
class SettingsStore;
class UpdateController;

class ApplicationController : public QObject {
    Q_OBJECT

public:
    explicit ApplicationController(bool popupOnly,
                                   std::shared_ptr<const PlatformComposition> platform = platformComposition(),
                                   QObject *parent = nullptr);

    // The front end outlives the controller and is attached after both exist,
    // because a front end needs the controller it renders.
    void setFrontEnd(AppFrontEnd *frontEnd);
    // The session the front end renders. Everything it shows about a dictation
    // arrives on these signals.
    DictationSession *session() const;
    // Called by the front end once its first window is on screen. Startup work
    // that would compete with the first paint waits for this.
    void frontEndReady();

    // True when the process runs without a main window of its own, so the
    // front end shows only the dictation popup.
    bool popupOnly() const;
    SettingsStore *settings() const;
    UpdateController *updates() const;
    QString pendingWhatsNewVersion() const;
    void clearPendingWhatsNew();
    SecretStore *secretStore() const;
    ProviderRegistry *providerRegistry() const;
    const PlatformComposition *platform() const;
    QString stateName() const;
    IpcResponse response(bool ok = true, const QString &message = {}) const;
    QString outputSummary() const;
    bool accessibilitySupported() const;
    bool accessibilityEnabled() const;
    bool accessibilityPersistent() const;
    bool enableAccessibility(QString *error = nullptr);
    // Platforms that do not push grants to a running process poll this.
    void refreshAccessibilityState();
    bool grabMainWindow(const QString &path) const;
    bool globalShortcutsSupported() const;
    bool globalShortcutSupportKnown() const;
    bool globalShortcutUsesDesktopChooser() const;
    QKeySequence globalShortcut() const;
    QString globalShortcutDisplay() const;
    bool setGlobalShortcut(const QKeySequence &shortcut, QString *error = nullptr);
    void registerGlobalShortcut();
    // Forgets the desktop's registration of the shortcut, where it keeps one.
    bool removeGlobalShortcutRegistration(QString *error = nullptr);

    void showMainWindow();
    void showSettingsWindow();
    void showSetupAssistant();
    void showSetupAssistant(SetupAssistantPage page);
    bool startIpc(QString *error = nullptr);

public slots:
    void toggle();
    void startListening();
    void stopListening();
    void showMain();
    void showSettings();
    void showSetup();
    void quitApplication();
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
    void globalShortcutChanged();
    void globalShortcutSupportChanged();
    void globalShortcutRegistrationFinished(bool bound, const QString &detail);
    void whatsNewChanged();
    void quitRequested();

private:
    void registerProviders();
    void startWithMicrophone(std::function<void()> start);
    void runDeferredStartup();
    bool ensureSetupCompleted();
    bool sessionActive() const;
    void handleShortcutPressed();
    void handleShortcutReleased();

    bool m_popupOnly = false;
    std::shared_ptr<const PlatformComposition> m_platform;
    AppFrontEnd *m_frontEnd = nullptr;
    SettingsStore *m_settings = nullptr;
    SecretStore *m_secrets = nullptr;
    ProviderRegistry *m_providers = nullptr;
    AudioInput *m_audio = nullptr;
    DictationSession *m_session = nullptr;
    UpdateController *m_updates = nullptr;
    GlobalShortcutBinder *m_shortcutBinder = nullptr;
    SingleInstanceIpc *m_ipc = nullptr;
    bool m_accessibilitySupported = false;
    bool m_accessibilityEnabled = false;
    bool m_accessibilityPersistent = false;
    QString m_pendingWhatsNewVersion;
#ifdef Q_OS_MACOS
    QTimer *m_accessibilityPoll = nullptr;
#endif
    bool m_deferredStartupScheduled = false;
    bool m_deferredStartupDone = false;
    // Only a press that started a session can end it on release; a press that
    // stopped one must not restart it.
    QElapsedTimer m_shortcutPress;
    bool m_shortcutStartedSession = false;
};

} // namespace speecher
