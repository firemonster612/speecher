#pragma once

#include <functional>
#include <memory>
#include <optional>

#include <QDate>
#include <QElapsedTimer>
#include <QObject>

#include "app/SingleInstanceIpc.h"
#include "core/DictationRecord.h"
#include "core/ShortcutBinding.h"
#include "platform/GlobalShortcutBinder.h"

class QLocalSocket;
class QTimer;

namespace speecher {

class AppFrontEnd;
enum class SetupAssistantPage;
class DictationSession;
class FileTranscriptionSession;
struct TranscribeOptions;
class AudioInput;
class CancelKeyGrab;
class InsightsLog;
class LocalModelStore;
class LocalSetup;
class ProviderRegistry;
class SecretStore;
class SettingsStore;
class UpdateBanner;
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
    // Why a batch cannot start now, or empty when it can.
    QString fileTranscriptionRefusal() const;
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
    // What every update banner and the Check for updates row show.
    UpdateBanner *updateBanner() const;
    InsightsLog *insightsLog() const;
    // The day Home summarizes up to. SPEECHER_INSIGHTS_TODAY pins it for
    // screenshots, so front ends ask here rather than reading the clock.
    QDate insightsToday() const;
    // What insights recorded of the last delivered transcript: its app and
    // day. Empty while insights did not record it.
    const std::optional<DictationRecord> &lastRecord() const;
    // The Clear insights history action, once the person has confirmed it.
    // False when the history file could not be deleted.
    bool clearInsights();
    QString pendingWhatsNewVersion() const;
    void clearPendingWhatsNew();
    SecretStore *secretStore() const;
    ProviderRegistry *providerRegistry() const;
    // Owned here so a download outlives whichever window started it.
    LocalModelStore *localModelStore() const;
    // Hardware, runners, endpoint checks, Speed Tests and Ollama pulls, owned
    // here for the same reason.
    LocalSetup *localSetup() const;
    const PlatformComposition *platform() const;
    QString stateName() const;
    // What a status line says about dictation now (dictationStatusLabel);
    // statusChanged carries it.
    QString statusLabel() const;
    // The last transcript Speecher heard, which tray panels offer to copy
    // again: the running preview, then the text as delivered. It outlives the
    // dictation that produced it, including a failed one, so it is empty only
    // until the first dictation says something.
    QString lastTranscript() const;
    IpcResponse response(bool ok = true, const QString &message = {}) const;
    QString outputSummary() const;
    bool accessibilitySupported() const;
    bool accessibilityEnabled() const;
    bool accessibilityPersistent() const;
    bool enableAccessibility(QString *error = nullptr);
    // Platforms that do not push grants to a running process poll this.
    void refreshAccessibilityState();
    bool grabMainWindow(const QString &path) const;
    // Each Global Shortcut question and change names the shortcut it is about:
    // the one that toggles dictation unless it says otherwise.
    bool globalShortcutsSupported(GlobalShortcutRole role = GlobalShortcutRole::Dictation) const;
    bool globalShortcutSupportKnown(GlobalShortcutRole role = GlobalShortcutRole::Dictation) const;
    bool globalShortcutUsesDesktopChooser(GlobalShortcutRole role = GlobalShortcutRole::Dictation) const;
    // Empty when the bound backend can honour this binding, otherwise what to
    // tell the user. Recorders ask before saving so a refusal is explained
    // rather than silently never firing.
    QString globalShortcutUnsupportedBindingReason(const ShortcutBinding &binding,
                                                   GlobalShortcutRole role = GlobalShortcutRole::Dictation) const;
    ShortcutBinding globalShortcut(GlobalShortcutRole role = GlobalShortcutRole::Dictation) const;
    QString globalShortcutDisplay(GlobalShortcutRole role = GlobalShortcutRole::Dictation) const;
    // Refuses the other Global Shortcut's binding; an empty one clears it.
    bool setGlobalShortcut(const ShortcutBinding &shortcut,
                           QString *error = nullptr,
                           GlobalShortcutRole role = GlobalShortcutRole::Dictation);
    // False once a press has proved that the bound backend never reports key
    // release, which is what makes push-to-talk behave as toggle. Answered from
    // what the shortcut has already done, never probed; it starts out true.
    bool globalShortcutReportsRelease() const;
    // False once this computer has refused a launch-at-login change, which is
    // what puts the caution beside the toggle. A later change it accepts clears
    // it again.
    bool launchAtLoginAccepted() const;
    // Lets a shortcut recorder see the bound combinations, and Escape, as key
    // events.
    void suspendGlobalShortcut();
    // The error of a shortcut that could not be taken back, and in failedRole
    // which one it was, so the front end shows it under that shortcut's row.
    QString resumeGlobalShortcut(GlobalShortcutRole *failedRole = nullptr);
    void registerGlobalShortcut(GlobalShortcutRole role = GlobalShortcutRole::Dictation);
    // Forgets the desktop's registration of the shortcut, where it keeps one.
    bool removeGlobalShortcutRegistration(QString *error = nullptr, GlobalShortcutRole role = GlobalShortcutRole::Dictation);

    void showMainWindow();
    // What a plain launch shows. Files opened during startup, before anyone
    // asked for this window, take it off screen again: a launch that opens
    // files shows the Transcribe window alone even when they arrive late.
    void showDefaultMainWindow();
    void showSettingsWindow();
    void showSetupAssistant();
    void showSetupAssistant(SetupAssistantPage page);
    // Opens the Transcribe page with these files listed, not yet started.
    // Before setup is complete it shows the setup assistant instead and holds
    // the files until completeSetup().
    void showTranscribeFiles(const QStringList &paths);
    // Whether any files have been opened this run.
    bool filesOpened() const;
    // Whether files opened before setup was complete are about to open now
    // that it is, in which case the Transcribe window is all they need.
    bool heldFilesOpening() const;
    // Records that the setup assistant finished, then opens any files that
    // arrived while it was up.
    void completeSetup();
    // The setup assistant closed, finished or not: the main window shows,
    // unless files held through setup are about to open in their own.
    void setupAssistantClosed();
    bool startIpc(QString *error = nullptr);

public slots:
    void toggle();
    void startListening();
    void stopListening();
    // Throws away the dictation in progress, or one about to start.
    void cancel();
    void showMain();
    void showSettings();
    void showSetup();
    void quitApplication();
    void handleIpcCommand(const QString &command,
                          const QString &outputFormat,
                          QLocalSocket *socket,
                          const QStringList &files = {},
                          const QString &writingProfile = {});

signals:
    void stateChanged(const QString &stateName);
    void statusChanged(const QString &status);
    void previewChanged(const QString &preview);
    void transcriptDelivered(const QString &text);
    void lastTranscriptChanged(const QString &text);
    void lastRecordChanged();
    void audioLevelChanged(float level);
    void accessibilityStateChanged(bool supported, bool enabled, bool persistent);
    void globalShortcutChanged();
    void globalShortcutSupportChanged();
    void globalShortcutReleaseSupportChanged();
    void launchAtLoginAcceptedChanged();
    void globalShortcutRegistrationFinished(bool bound, const QString &detail, GlobalShortcutRole role);
    void whatsNewChanged();
    void quitRequested();

private:
    void forgetLastRecord();
    void notifyModelReady(const QString &modelId);
    void startWithMicrophone(std::function<void()> start);
    void runDeferredStartup();
    bool ensureSetupCompleted();
    bool sessionActive() const;
    void handleShortcutPressed();
    void handleShortcutReleased(qint64 heldMs);
    void forgetShortcutGesture();
    GlobalShortcutBinder *shortcutBinder(GlobalShortcutRole role) const;
    // Escape is taken while a session can be cancelled and nothing records keys.
    void updateCancelKeyGrab();
    void dropPendingStart();
    void setLaunchAtLoginAccepted(bool accepted);

    bool m_popupOnly = false;
    std::shared_ptr<const PlatformComposition> m_platform;
    AppFrontEnd *m_frontEnd = nullptr;
    SettingsStore *m_settings = nullptr;
    SecretStore *m_secrets = nullptr;
    ProviderRegistry *m_providers = nullptr;
    LocalModelStore *m_localModels = nullptr;
    LocalSetup *m_localSetup = nullptr;
    AudioInput *m_audio = nullptr;
    DictationSession *m_session = nullptr;
    FileTranscriptionSession *m_fileTranscription = nullptr;
    // Files opened before setup was complete.
    QStringList m_pendingTranscribeFiles;
    bool m_filesOpened = false;
    // The main window is up only because a plain launch shows it.
    bool m_mainWindowByDefault = false;
    UpdateController *m_updates = nullptr;
    UpdateBanner *m_updateBanner = nullptr;
    InsightsLog *m_insightsLog = nullptr;
    std::optional<DictationRecord> m_lastRecord;
    QString m_lastTranscript;
    QDate m_insightsToday;
    GlobalShortcutBinder *m_shortcutBinder = nullptr;
    GlobalShortcutBinder *m_cancelShortcutBinder = nullptr;
    CancelKeyGrab *m_cancelKeyGrab = nullptr;
    int m_shortcutSuspensions = 0;
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
