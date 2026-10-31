#pragma once

#include <functional>
#include <memory>
#include <optional>

#include <QDate>
#include <QObject>

#include "app/PlatformComposition.h"
#include "app/SingleInstanceIpc.h"
#include "core/DictationRecord.h"
#include "core/ShortcutBinding.h"
#include "dictation/DictationTypes.h"
#include "platform/GlobalShortcutBinder.h"

namespace speecher {

class AccessibilityMonitor;
class AppFrontEnd;
enum class SetupAssistantPage;
class DictationSession;
enum class DictationState;
class FileTranscriptionSession;
class Accounts;
class TranscribeQueue;
class AudioInput;
class SessionKeyGrab;
class ShortcutGesture;
class InsightsLog;
class IpcCommandHandler;
class LocalModelStore;
class LocalSetup;
class NetworkReachability;
class ProviderAvailability;
class ProviderRegistry;
class RecordingSession;
class SecretStore;
class SettingsStore;
class UpdateController;
class WhatsNewTracker;

class ApplicationController : public QObject {
    Q_OBJECT

public:
    // ipc is the socket the command line reaches the controller on: main
    // hands over one already listening, so a second launch forwards its
    // command before anything is built. Without one, the controller makes
    // its own, which ipcCommands()->listen() opens.
    explicit ApplicationController(bool popupOnly,
                                   std::shared_ptr<const PlatformComposition> platform = platformComposition(),
                                   std::unique_ptr<SingleInstanceIpc> ipc = {},
                                   QObject *parent = nullptr);
    ~ApplicationController() override;

    // A front end needs the controller it renders, so it attaches once both
    // exist, and detaches with nullptr before it goes.
    void setFrontEnd(AppFrontEnd *frontEnd);
    // The session the front end renders. Everything it shows about a dictation
    // arrives on these signals.
    DictationSession *session() const;
    // The batch the Transcribe page drives. One runs at a time, and it and
    // dictation exclude each other: a batch will not start while a dictation
    // is under way, and dictation will not start while a batch runs.
    FileTranscriptionSession *fileTranscription() const;
    // The Transcribe page's files, choices and results, made on first use
    // and kept here so an update restart can wait for its results.
    TranscribeQueue *transcribeQueue();
    // Why a batch cannot start now, or empty when it can.
    QString fileTranscriptionRefusal() const;
    // A recording, which `speecher record` starts and stops, runs beside
    // dictation and file transcription with a microphone input of its own.
    bool isRecording() const;
    // Starts a recording once setup is complete and the microphone is allowed.
    // done gets why it did not start, or nothing once it records; macOS may
    // ask for the microphone first, and the provider's stream must connect, so
    // it can come long after.
    void startRecording(const QString &path,
                        const QStringList &vocabulary,
                        bool microphoneOnly,
                        std::function<void(const QString &error)> done);
    // Whether the setup assistant still has to run before dictation, recording
    // or a window opens. Onboarding happens in the front end, so with none
    // attached nothing waits for it; each start still checks its provider and
    // the microphone.
    bool setupPending() const;
    // Called by the front end once its first window is on screen. Startup work
    // that would compete with the first paint waits for this.
    void frontEndReady();

    // True when the process runs without a main window of its own, so the
    // front end shows only the dictation popup.
    bool popupOnly() const;
    SettingsStore *settings() const;
    UpdateController *updates() const;
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
    WhatsNewTracker *whatsNew() const;
    SecretStore *secretStore() const;
    ProviderRegistry *providerRegistry() const;
    // Owned here so a download outlives whichever window started it.
    LocalModelStore *localModelStore() const;
    // Hardware, runners, endpoint checks, Speed Tests and Ollama pulls, owned
    // here for the same reason.
    LocalSetup *localSetup() const;
    // Whether this computer is online, shared by settings and dictation.
    NetworkReachability *networkReachability() const;
    // Where the sign-ins learned along the way are noted for settings status.
    ProviderAvailability *providerAvailability() const;
    // Each service's sign-in and which secrets are saved, checked once a
    // window first asks.
    Accounts *accounts() const;
    const PlatformComposition *platform() const;
    // The Dictation Session's state.
    DictationState dictationState() const;
    // What a status line says about dictation now (dictationStatusLabel);
    // statusChanged carries it.
    QString statusLabel() const;
    // The last transcript Speecher heard, which `speecher last` prints and
    // Home offers to copy again: the running preview, then the text as
    // delivered. It outlives the dictation that produced it, including a
    // failed one, so it is empty only until the first dictation says
    // something.
    QString lastTranscript() const;
    QString outputSummary() const;
    AccessibilityMonitor *accessibility() const;
    // Each Global Shortcut question and change names the shortcut it is about:
    // the one that toggles dictation unless it says otherwise.
    bool globalShortcutsSupported(GlobalShortcutRole role = GlobalShortcutRole::Dictation) const;
    bool globalShortcutSupportKnown(GlobalShortcutRole role = GlobalShortcutRole::Dictation) const;
    bool globalShortcutUsesDesktopChooser(GlobalShortcutRole role = GlobalShortcutRole::Dictation) const;
    ShortcutBinding globalShortcut(GlobalShortcutRole role = GlobalShortcutRole::Dictation) const;
    QString globalShortcutDisplay(GlobalShortcutRole role = GlobalShortcutRole::Dictation) const;
    // Refuses another Global Shortcut's binding; an empty one clears it.
    bool setGlobalShortcut(const ShortcutBinding &shortcut,
                           QString *error = nullptr,
                           GlobalShortcutRole role = GlobalShortcutRole::Dictation);
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
    // After an update restart, reopens the main window if it was on screen
    // when the restart was asked for, and forgets that so no later launch
    // reopens it. Never the microphone: the relaunch has no dictation gesture
    // behind it.
    void reopenMainWindowAfterUpdate();
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
    // Serves the command line over the local socket once main() has it listen.
    IpcCommandHandler *ipcCommands() const;

public slots:
    void toggle();
    // A toggle with the --format, --profile or --language it was given.
    void toggleWith(const SessionOverrides &overrides);
    void startListening();
    // A start with the --format, --profile or --language it was given.
    void startListeningWith(const SessionOverrides &overrides);
    void stopListening();
    // Throws away the dictation in progress, or one about to start.
    void cancel();
    void showMain();
    void showSettings();
    void showSetup();
    void quitApplication();

signals:
    void stateChanged(DictationState state);
    void statusChanged(const QString &status);
    void previewChanged(const QString &preview);
    void transcriptDelivered(const QString &text);
    void lastTranscriptChanged(const QString &text);
    void lastRecordChanged();
    void recordingChanged(bool recording);
    // The Transcribe queue's results that aren't saved, exported or dismissed
    // changed, so what an update restart waits for may have too.
    void unsavedTranscriptsChanged();
    void audioLevelChanged(float level);
    void globalShortcutChanged();
    void globalShortcutSupportChanged();
    void globalShortcutRegistrationFinished(bool bound, const QString &detail, GlobalShortcutRole role);
    void quitRequested();

private:
    void forgetLastRecord();
    void notifyModelReady(const QString &modelId);
    void startWithMicrophone(std::function<void()> start);
    void runDeferredStartup();
    bool ensureSetupCompleted();
    // A dictation is under way, or a start waits for the microphone grant.
    bool sessionActive() const;
    GlobalShortcutBinder *shortcutBinder(GlobalShortcutRole role) const;
    // Arms the Cancel and Pause Shortcuts while their action applies, and
    // takes Escape while a session can be cancelled, nothing records keys and
    // neither of them is Escape.
    void updateSessionShortcuts();
    void dropPendingStart();

    bool m_popupOnly = false;
    std::shared_ptr<const PlatformComposition> m_platform;
    AppFrontEnd *m_frontEnd = nullptr;
    SettingsStore *m_settings = nullptr;
    SecretStore *m_secrets = nullptr;
    ProviderRegistry *m_providers = nullptr;
    LocalModelStore *m_localModels = nullptr;
    LocalSetup *m_localSetup = nullptr;
    NetworkReachability *m_reachability = nullptr;
    ProviderAvailability *m_availability = nullptr;
    Accounts *m_accounts = nullptr;
    AudioInput *m_audio = nullptr;
    DictationSession *m_session = nullptr;
    FileTranscriptionSession *m_fileTranscription = nullptr;
    TranscribeQueue *m_transcribeQueue = nullptr;
    RecordingSession *m_recording = nullptr;
    // Files opened before setup was complete.
    QStringList m_pendingTranscribeFiles;
    bool m_filesOpened = false;
    // The main window is up only because a plain launch shows it.
    bool m_mainWindowByDefault = false;
    UpdateController *m_updates = nullptr;
    InsightsLog *m_insightsLog = nullptr;
    std::optional<DictationRecord> m_lastRecord;
    QString m_lastTranscript;
    QDate m_insightsToday;
    GlobalShortcutBinder *m_shortcutBinder = nullptr;
    GlobalShortcutBinder *m_cancelShortcutBinder = nullptr;
    GlobalShortcutBinder *m_pauseShortcutBinder = nullptr;
    SessionKeyGrab *m_sessionKeyGrab = nullptr;
    int m_shortcutSuspensions = 0;
    IpcCommandHandler *m_ipcCommands = nullptr;
    ShortcutGesture *m_shortcutGesture = nullptr;
    WhatsNewTracker *m_whatsNew = nullptr;
    AccessibilityMonitor *m_accessibility = nullptr;
    bool m_deferredStartupScheduled = false;
    bool m_deferredStartupDone = false;
    quint64 m_microphoneStartGeneration = 0;
    bool m_microphoneStartPending = false;
    // Set for good once quitApplication() runs; guards its re-entry and
    // blocks new session starts during the shutdown pump.
    bool m_quitting = false;
};

} // namespace speecher
