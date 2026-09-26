#pragma once

#include <functional>
#include <memory>

#include <QElapsedTimer>
#include <QObject>

#include "app/SingleInstanceIpc.h"
#include "core/ShortcutBinding.h"

class QLocalSocket;
class QTimer;

namespace speecher {

class AppFrontEnd;
enum class SetupAssistantPage;
class DictationSession;
class FileTranscriptionSession;
struct TranscribeOptions;
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
    ~ApplicationController() override;

    // The front end outlives the controller and is attached after both exist,
    // because a front end needs the controller it renders.
    void setFrontEnd(AppFrontEnd *frontEnd);
    // The session the front end renders. Everything it shows about a dictation
    // arrives on these signals.
    DictationSession *session() const;
    // The batch the Transcribe page drives. One runs at a time, and it and
    // dictation exclude each other: a batch will not start while a dictation
    // is under way, and dictation will not start while a batch runs.
    FileTranscriptionSession *fileTranscription() const;
    bool startFileTranscription(const QStringList &paths,
                                const TranscribeOptions &options,
                                QString *error = nullptr);
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
    // Empty when the bound backend can honour this binding, otherwise what to
    // tell the user. Recorders ask before saving so a refusal is explained
    // rather than silently never firing.
    QString globalShortcutUnsupportedBindingReason(const ShortcutBinding &binding) const;
    ShortcutBinding globalShortcut() const;
    QString globalShortcutDisplay() const;
    bool setGlobalShortcut(const ShortcutBinding &shortcut, QString *error = nullptr);
    // False once a press has proved that the bound backend never reports key
    // release, which is what makes push-to-talk behave as toggle. Answered from
    // what the shortcut has already done, never probed; it starts out true.
    bool globalShortcutReportsRelease() const;
    // False once this computer has refused a launch-at-login change, which is
    // what puts the caution beside the toggle. A later change it accepts clears
    // it again.
    bool launchAtLoginAccepted() const;
    // Lets a shortcut recorder see the bound combination as a key event.
    void suspendGlobalShortcut();
    QString resumeGlobalShortcut();
    void registerGlobalShortcut();
    // Forgets the desktop's registration of the shortcut, where it keeps one.
    bool removeGlobalShortcutRegistration(QString *error = nullptr);

    void showMainWindow();
    void showSettingsWindow();
    void showSetupAssistant();
    void showSetupAssistant(SetupAssistantPage page);
    // Opens the Transcribe page with these files listed, not yet started.
    // Before setup is complete it shows the setup assistant instead and holds
    // the files until completeSetup().
    void showTranscribeFiles(const QStringList &paths);
    // Records that the setup assistant finished, then opens any files that
    // arrived while it was up.
    void completeSetup();
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
                          QLocalSocket *socket,
                          const QStringList &files = {});

signals:
    void stateChanged(const QString &stateName);
    void statusChanged(const QString &status);
    void previewChanged(const QString &preview);
    void transcriptDelivered(const QString &text);
    void audioLevelChanged(float level);
    void accessibilityStateChanged(bool supported, bool enabled, bool persistent);
    void globalShortcutChanged();
    void globalShortcutSupportChanged();
    void globalShortcutReleaseSupportChanged();
    void launchAtLoginAcceptedChanged();
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
    void forgetShortcutGesture();
    void setLaunchAtLoginAccepted(bool accepted);

    bool m_popupOnly = false;
    std::shared_ptr<const PlatformComposition> m_platform;
    AppFrontEnd *m_frontEnd = nullptr;
    SettingsStore *m_settings = nullptr;
    SecretStore *m_secrets = nullptr;
    ProviderRegistry *m_providers = nullptr;
    AudioInput *m_audio = nullptr;
    DictationSession *m_session = nullptr;
    FileTranscriptionSession *m_fileTranscription = nullptr;
    // Files opened before setup was complete.
    QStringList m_pendingTranscribeFiles;
    UpdateController *m_updates = nullptr;
    GlobalShortcutBinder *m_shortcutBinder = nullptr;
    SingleInstanceIpc *m_ipc = nullptr;
    bool m_accessibilitySupported = false;
    bool m_accessibilityEnabled = false;
    bool m_accessibilityPersistent = false;
    bool m_launchAtLoginAccepted = true;
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
    bool m_shortcutDown = false;
    bool m_shortcutReleaseSeen = false;
    bool m_shortcutPressedWhileDown = false;
    QTimer *m_pushToTalkStart = nullptr;
    quint64 m_microphoneStartGeneration = 0;
    bool m_microphoneStartPending = false;
    // Set for good once quitApplication() runs; guards its re-entry and
    // blocks new session starts during the shutdown pump.
    bool m_quitting = false;
};

} // namespace speecher
