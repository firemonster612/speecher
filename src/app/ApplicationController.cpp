#include "app/ApplicationController.h"

#include "app/AccessibilityMonitor.h"
#include "app/Accounts.h"
#include "app/AppFrontEnd.h"
#include "app/IpcCommandHandler.h"
#include "app/LocalSetup.h"
#include "app/NetworkReachability.h"
#include "app/ProviderAvailability.h"
#include "app/ProviderSetup.h"
#include "app/ShortcutGesture.h"
#include "app/ShortcutSuspendingDelivery.h"
#include "app/UpdateController.h"
#include "app/WhatsNewTracker.h"
#ifdef Q_OS_MACOS
#include "app/MacSparkleUpdater.h"
#elif defined(Q_OS_WIN)
#include "app/WindowsInstallerUpdater.h"
#else
#include "app/AppImageUpdater.h"
#endif
#include "core/InsightsLog.h"
#include "core/ProviderTable.h"
#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#include "core/settings/SettingsKeys.h"
#include "dictation/DictationSession.h"
#include "providers/LocalModelStore.h"
#include "providers/ProviderRegistry.h"
#include "recording/RecordingPresentation.h"
#include "recording/RecordingSession.h"
#include "platform/SessionKeyGrab.h"
#include "platform/GlobalShortcutBinder.h"
#include "transcribe/FileTranscriptionSession.h"
#include "transcribe/TranscribeQueue.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QStandardPaths>
#include <QTimer>
#ifdef Q_OS_MACOS
#include <QPermissions>
#endif

#include <algorithm>
#include <utility>

namespace speecher {
namespace {

// How long a quit mid-dictation waits for the asynchronous media-resume calls
// to leave the process before the event loop stops for good.
constexpr int mediaResumeGraceMs = 200;
// How long after launch opened files still replace the default main window.
constexpr int defaultMainWindowGraceMs = 3000;
// How long an update restart may take to relaunch. A request to reopen the
// main window older than this was left by a restart that never relaunched.
constexpr qint64 updateRelaunchMaxAgeMs = 120000;

QString microphoneAccessOffText()
{
    return QStringLiteral("Microphone access is off. Allow Speecher under Privacy & Security > "
                          "Microphone, then try again.");
}

// Why a Global Shortcut cannot take the binding another one already has.
QString shortcutTakenText(GlobalShortcutRole takenBy)
{
    switch (takenBy) {
    case GlobalShortcutRole::Cancel:
        return QStringLiteral("That is already the Cancel Shortcut.");
    case GlobalShortcutRole::Pause:
        return QStringLiteral("That is already the Pause Shortcut.");
    case GlobalShortcutRole::Dictation:
        break;
    }
    return QStringLiteral("That is already the Global Shortcut for dictation.");
}

// Where recordings, insights and Local Models are kept.
QString dataFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}

} // namespace

ApplicationController::ApplicationController(bool popupOnly,
                                             std::shared_ptr<const PlatformComposition> platform,
                                             QObject *parent)
    : QObject(parent)
    , m_popupOnly(popupOnly)
    , m_platform(std::move(platform))
    , m_settings(new SettingsStore(this))
    , m_secrets(m_settings->secrets())
    , m_providers(new ProviderRegistry(this))
    , m_localModels(new LocalModelStore(this))
    , m_shortcutBinder(m_platform->createGlobalShortcutBinder(GlobalShortcutRole::Dictation, m_settings, this))
    , m_cancelShortcutBinder(m_platform->createGlobalShortcutBinder(GlobalShortcutRole::Cancel, m_settings, this))
    , m_pauseShortcutBinder(m_platform->createGlobalShortcutBinder(GlobalShortcutRole::Pause, m_settings, this))
    , m_sessionKeyGrab(m_platform->createSessionKeyGrab(this))
    , m_shortcutGesture(new ShortcutGesture(*m_settings, [this] { return sessionActive(); }, this))
    , m_whatsNew(new WhatsNewTracker(*m_settings, this))
{
    // The login item is the person's, which an isolated instance leaves alone.
    if (!SettingsKeys::isolatedInstance()) {
        m_settings->setLaunchAtLoginReconciler(
            [this](bool enabled, QString *error) { return m_platform->setLaunchAtLogin(enabled, error); });
        m_settings->reconcileLaunchAtLogin();
    }
    connect(m_shortcutBinder, &GlobalShortcutBinder::activated, m_shortcutGesture, &ShortcutGesture::press);
    connect(m_shortcutBinder, &GlobalShortcutBinder::deactivated, m_shortcutGesture, &ShortcutGesture::release);
    connect(m_shortcutGesture, &ShortcutGesture::toggleRequested, this, &ApplicationController::toggle);
    connect(m_shortcutGesture, &ShortcutGesture::startRequested, this, &ApplicationController::startListening);
    connect(m_shortcutGesture, &ShortcutGesture::stopRequested, this, &ApplicationController::stopListening);
    connect(m_shortcutBinder, &GlobalShortcutBinder::bindingChanged, this, [this] {
        m_shortcutGesture->forget();
        emit globalShortcutChanged();
    });
    connect(m_shortcutBinder,
            &GlobalShortcutBinder::supportChanged,
            this,
            &ApplicationController::globalShortcutSupportChanged);
    connect(m_shortcutBinder,
            &GlobalShortcutBinder::registrationFinished,
            this,
            [this](bool bound, const QString &detail) {
                emit globalShortcutRegistrationFinished(bound, detail, GlobalShortcutRole::Dictation);
            });
    // The Cancel and Pause Shortcuts act on press only; their release means
    // nothing. Outside a Dictation Session both do nothing.
    connect(m_cancelShortcutBinder, &GlobalShortcutBinder::activated, this, &ApplicationController::cancel);
    for (const GlobalShortcutRole role : {GlobalShortcutRole::Cancel, GlobalShortcutRole::Pause}) {
        GlobalShortcutBinder *binder = shortcutBinder(role);
        connect(binder, &GlobalShortcutBinder::bindingChanged, this, [this] {
            updateSessionShortcuts();
            emit globalShortcutChanged();
        });
        connect(binder, &GlobalShortcutBinder::supportChanged,
                this, &ApplicationController::globalShortcutSupportChanged);
        connect(binder,
                &GlobalShortcutBinder::registrationFinished,
                this,
                [this, role](bool bound, const QString &detail) {
                    emit globalShortcutRegistrationFinished(bound, detail, role);
                });
    }
    if (m_sessionKeyGrab) {
        connect(m_sessionKeyGrab, &SessionKeyGrab::pressed, this, [this](SessionKeyGrab::Key key) {
            if (key == SessionKeyGrab::Key::Enter) {
                m_session->replaceSelection();
            } else {
                cancel();
            }
        });
    }
    m_secrets->migrateSettingsFallbacks();
    m_secrets->prefetch();
    registerProviders(*m_providers, m_secrets, m_localModels);
    m_localSetup = new LocalSetup(*m_settings, *m_providers, *m_localModels, this);
    m_reachability = new NetworkReachability(this);
    m_reachability->watchSystem();
    m_availability = new ProviderAvailability(*m_reachability, this);
    m_localSetup->setProviderAvailability(*m_availability);
    m_accounts = new Accounts(*m_settings, *m_availability, *m_providers, this);
    connect(m_localModels, &LocalModelStore::downloadFinished,
            this, &ApplicationController::notifyModelReady);
    TargetProvider *targetProvider = m_platform->createTargetProvider(this);
    targetProvider->setCorrectionObservationEnabled(m_settings->dictationSnapshot().correctionLearningEnabled);
    connect(m_settings,
            &SettingsStore::correctionLearningEnabledChanged,
            targetProvider,
            [targetProvider](bool enabled) {
                targetProvider->setCorrectionObservationEnabled(enabled);
            });
    connect(targetProvider,
            &TargetProvider::correctionObserved,
            this,
            [this](const QString &original,
                   const QString &corrected,
                   const QString &applicationId,
                   double confidence) {
                // It records nothing while correction learning is off.
                m_settings->recordCorrectionEvidence({original, corrected, confidence}, applicationId);
            });
    m_audio = m_platform->createAudioInput(m_settings, this);
    m_session = new DictationSession(m_settings,
                                     m_audio,
                                     m_platform->createMediaController(this),
                                     targetProvider,
                                     new ShortcutSuspendingDelivery(
                                         m_platform->createTextDelivery(targetProvider, this),
                                         {m_shortcutBinder, m_cancelShortcutBinder, m_pauseShortcutBinder},
                                         this),
                                     m_providers,
                                     this);
    m_session->setScreenshotContextProvider(
        m_platform->createScreenshotContextProvider(this));
    m_session->setReachability(m_reachability->reachability());
    connect(m_reachability, &NetworkReachability::changed, m_session,
            [this] { m_session->setReachability(m_reachability->reachability()); });
    connect(m_session, &DictationSession::providerSignInObserved, m_availability, &ProviderAvailability::noteSignIn);
    m_fileTranscription = new FileTranscriptionSession(m_settings, m_providers, this);
    m_recording = new RecordingSession(
        m_settings, m_providers,
        [this](QObject *parent) { return m_platform->createRecordingAudioInput(m_settings, parent); },
        m_platform->recordsSystemAudio()
            ? RecordingSession::InputFactory([this](QObject *parent) { return m_platform->createSystemAudioInput(parent); })
            : RecordingSession::InputFactory(),
        [this](QString *warning) { return m_platform->createEchoCanceller(warning); }, this);
    connect(m_recording, &RecordingSession::recordingChanged, this, &ApplicationController::recordingChanged);
    connect(m_session, &DictationSession::microphoneChanged, m_recording, &RecordingSession::setDictating);
    connect(m_session, &DictationSession::listeningChanged, m_recording, &RecordingSession::setDictationListening);
#ifdef Q_OS_MACOS
    m_updates = new MacSparkleUpdater(m_settings, this);
#elif defined(Q_OS_WIN)
    m_updates = new WindowsInstallerUpdater(m_settings, this);
#else
    m_updates = new AppImageUpdater(m_settings, this);
#endif
    // A restart for an update waits out a dictation, a recording and a file
    // transcription, with the transcripts it made that are only on screen. A
    // recording still starting does not hold it back: a start that fails
    // emits no stop to resume the restart on.
    m_updates->setRestartBlockerProvider([this] {
        using RestartBlocker = UpdateController::RestartBlocker;
        const DictationState state = m_session->state();
        if (state != DictationState::Idle && state != DictationState::Error) {
            return RestartBlocker::Dictation;
        }
        if (m_recording->isRecording()) {
            return RestartBlocker::Recording;
        }
        if (m_fileTranscription->isRunning()) {
            return RestartBlocker::FileTranscription;
        }
        return m_transcribeQueue && m_transcribeQueue->holdsUnsavedResults() ? RestartBlocker::UnsavedTranscripts
                                                                             : RestartBlocker::None;
    });
    connect(m_session, &DictationSession::stateChanged, m_updates, &UpdateController::resumePendingRestart);
    connect(m_fileTranscription, &FileTranscriptionSession::batchFinished, m_updates,
            &UpdateController::resumePendingRestart);

    // A seed log stands in for real history in screenshots and demos, so it
    // is never written; a pinned today makes those screenshots repeatable.
    const QString insightsSeed = qEnvironmentVariable("SPEECHER_INSIGHTS_SEED");
    m_insightsLog = insightsSeed.isEmpty()
        ? new InsightsLog(dataFolder() + QStringLiteral("/insights.jsonl"),
                          InsightsLog::Access::ReadWrite,
                          this)
        : new InsightsLog(insightsSeed, InsightsLog::Access::ReadOnly, this);
    m_insightsToday =
        QDate::fromString(qEnvironmentVariable("SPEECHER_INSIGHTS_TODAY"), Qt::ISODate);
    connect(m_session, &DictationSession::dictationRecorded, m_insightsLog, &InsightsLog::append);
    // The record describes the transcript Home shows, so it goes whenever
    // that transcript does: a new session starting (which drops the last
    // transcript, delivered or not) and a delivery. The session reports a
    // delivery first, then (while insights record it) its record.
    connect(m_session, &DictationSession::stateChanged, this, [this] {
        if (m_session->state() == DictationState::Starting) {
            forgetLastRecord();
        }
    });
    connect(m_session, &DictationSession::transcriptDelivered, this, &ApplicationController::forgetLastRecord);
    connect(m_session, &DictationSession::dictationRecorded, this, [this](const DictationRecord &record) {
        m_lastRecord = record;
        emit lastRecordChanged();
    });

    m_ipcCommands = new IpcCommandHandler(*this, *m_recording, m_platform, this);
    // After record stop's replies are written: recordingChanged(false) comes
    // before them.
    connect(m_recording, &RecordingSession::stopped, m_updates, &UpdateController::resumePendingRestart);
    connect(m_session, &DictationSession::stateChanged, this, &ApplicationController::stateChanged);
    connect(m_pauseShortcutBinder, &GlobalShortcutBinder::activated, m_session, &DictationSession::togglePause);
    connect(m_session, &DictationSession::stateChanged, this, &ApplicationController::updateSessionShortcuts);
#ifdef Q_OS_MACOS
    connect(m_session, &DictationSession::stateChanged, this, [this](DictationState state) {
        if (state != DictationState::Listening) {
            return;
        }
        const std::optional<float> volume = m_platform->inputVolume();
        if (volume) {
            qInfo().noquote() << "macOS default input volume="
                              + QString::number(qRound(*volume * 100.0f)) + "%";
        }
    });
#endif
    connect(m_session, &DictationSession::statusChanged, this, [this] { emit statusChanged(statusLabel()); });
    connect(m_session, &DictationSession::previewChanged, this, &ApplicationController::previewChanged);
    connect(m_session, &DictationSession::transcriptDelivered, this, &ApplicationController::transcriptDelivered);
    // A session start clears the preview; the last transcript stays until
    // the new dictation says something.
    const auto keepTranscript = [this](const QString &text) {
        if (text.isEmpty() || text == m_lastTranscript) {
            return;
        }
        m_lastTranscript = text;
        emit lastTranscriptChanged(text);
    };
    connect(m_session, &DictationSession::previewChanged, this, keepTranscript);
    connect(m_session, &DictationSession::transcriptDelivered, this, keepTranscript);
    connect(m_session, &DictationSession::audioLevelChanged, this, &ApplicationController::audioLevelChanged);
    // Pause and resume make no sound; only starting and stopping do.
    connect(m_session, &DictationSession::stateChanged, this, [this, wasPaused = false]() mutable {
        const DictationState state = m_session->state();
        const bool resumed = std::exchange(wasPaused, state == DictationState::Paused)
            && state == DictationState::Listening;
        // Read last: on Windows a settings read is many registry lookups, and
        // most state changes make no sound anyway.
        if (!resumed && (state == DictationState::Listening || state == DictationState::Stopping)
            && m_settings->dictationSnapshot().ui.soundsEnabled) {
            if (m_frontEnd) {
                m_frontEnd->alert();
            }
        }
    });
    m_accessibility = new AccessibilityMonitor(*m_platform, this);
    // A process that never shows a window still has to warm up, so the wait for
    // the front end is capped rather than open-ended.
    QTimer::singleShot(2000, this, &ApplicationController::runDeferredStartup);
}

void ApplicationController::setFrontEnd(AppFrontEnd *frontEnd)
{
    m_frontEnd = frontEnd;
}

DictationSession *ApplicationController::session() const
{
    return m_session;
}

FileTranscriptionSession *ApplicationController::fileTranscription() const
{
    return m_fileTranscription;
}

// Made on first use rather than at startup: seeding its choices reads the
// keyring, which a launch that hands its command to the running instance
// never needs.
TranscribeQueue *ApplicationController::transcribeQueue()
{
    if (!m_transcribeQueue) {
        m_transcribeQueue = new TranscribeQueue(m_settings, m_providers, m_fileTranscription,
                                                [this] { return fileTranscriptionRefusal(); }, this);
        connect(m_transcribeQueue, &TranscribeQueue::unsavedResultsChanged, m_updates,
                &UpdateController::resumePendingRestart);
        connect(m_transcribeQueue, &TranscribeQueue::unsavedResultsChanged, this,
                &ApplicationController::unsavedTranscriptsChanged);
    }
    return m_transcribeQueue;
}

QString ApplicationController::fileTranscriptionRefusal() const
{
    const DictationState state = m_session->state();
    return m_fileTranscription->isRunning()
        ? QStringLiteral("Files are already being transcribed.")
        : (state != DictationState::Idle && state != DictationState::Error) || m_microphoneStartPending
            ? QStringLiteral("Finish the dictation in progress, then transcribe the files.")
            : QString();
}

bool ApplicationController::isRecording() const
{
    return m_recording->isRecording();
}

void ApplicationController::startRecording(const QString &path,
                                           const QStringList &vocabulary,
                                           bool microphoneOnly,
                                           std::function<void(const QString &error)> done)
{
    if (!ensureSetupCompleted()) {
        done(recordingNeedsSetupText());
        return;
    }
    m_platform->requestMicrophoneAccess(this, [this, path, vocabulary, microphoneOnly,
                                               done = std::move(done)](bool granted) {
        if (!granted) {
            done(microphoneAccessOffText());
            return;
        }
        m_recording->start(path, vocabulary, dataFolder(), microphoneOnly, done);
    });
}

InsightsLog *ApplicationController::insightsLog() const
{
    return m_insightsLog;
}

QDate ApplicationController::insightsToday() const
{
    return m_insightsToday.isValid() ? m_insightsToday : QDate::currentDate();
}

const std::optional<DictationRecord> &ApplicationController::lastRecord() const
{
    return m_lastRecord;
}

bool ApplicationController::clearInsights()
{
    if (!m_insightsLog->clear()) {
        return false;
    }
    forgetLastRecord();
    return true;
}

void ApplicationController::forgetLastRecord()
{
    if (m_lastRecord) {
        m_lastRecord.reset();
        emit lastRecordChanged();
    }
}

bool ApplicationController::popupOnly() const
{
    return m_popupOnly;
}

void ApplicationController::frontEndReady()
{
    if (m_deferredStartupScheduled) {
        return;
    }
    m_deferredStartupScheduled = true;
    QTimer::singleShot(0, this, &ApplicationController::runDeferredStartup);
}

void ApplicationController::runDeferredStartup()
{
    if (m_deferredStartupDone) {
        return;
    }
    m_deferredStartupDone = true;
    // Opening the input device is what makes macOS raise the microphone prompt,
    // and a prompt nobody asked for at launch reads as an ambush. Warm up only
    // once the grant already exists; the first dictation asks for it properly.
#ifdef Q_OS_MACOS
    if (qApp->checkPermission(QMicrophonePermission{}) == Qt::PermissionStatus::Granted) {
        m_audio->warmUp();
    }
#else
    m_audio->warmUp();
#endif
    for (const GlobalShortcutRole role : globalShortcutRoles) {
        shortcutBinder(role)->bind();
    }
    m_accessibility->checkAtStartup();
}

SettingsStore *ApplicationController::settings() const
{
    return m_settings;
}

UpdateController *ApplicationController::updates() const
{
    return m_updates;
}

WhatsNewTracker *ApplicationController::whatsNew() const
{
    return m_whatsNew;
}

SecretStore *ApplicationController::secretStore() const
{
    return m_secrets;
}

LocalModelStore *ApplicationController::localModelStore() const
{
    return m_localModels;
}

LocalSetup *ApplicationController::localSetup() const
{
    return m_localSetup;
}

NetworkReachability *ApplicationController::networkReachability() const
{
    return m_reachability;
}

ProviderAvailability *ApplicationController::providerAvailability() const
{
    return m_availability;
}

Accounts *ApplicationController::accounts() const
{
    return m_accounts;
}

// A download the setup assistant left running finishes long after its window
// closed; nothing on screen would say dictation now works.
void ApplicationController::notifyModelReady(const QString &modelId)
{
    const LocalModel *model = findLocalModel(modelId);
    if (!m_frontEnd || !model) {
        return;
    }
    const SpeechSettings speech = m_settings->dictationSnapshot().speech;
    const bool inUse = parseProviderId(speech.providerId) == ProviderId::Local && speech.local.modelId == modelId;
    m_frontEnd->notifyIfNoWindowShown(
        QStringLiteral("%1 is ready").arg(model->name),
        inUse ? QStringLiteral("You can start dictating. Speech stays on this computer.")
              : QStringLiteral("Choose it on the Local models page to dictate with it."),
        QStringLiteral("models"));
}

ProviderRegistry *ApplicationController::providerRegistry() const
{
    return m_providers;
}

const PlatformComposition *ApplicationController::platform() const
{
    return m_platform.get();
}

DictationState ApplicationController::dictationState() const
{
    return m_session->reportedState();
}

QString ApplicationController::statusLabel() const
{
    return dictationStatusLabel(m_session->reportedState(), m_session->lastMessage());
}

QString ApplicationController::lastTranscript() const
{
    return m_lastTranscript;
}

QString ApplicationController::outputSummary() const
{
    return m_platform->outputSummary();
}

AccessibilityMonitor *ApplicationController::accessibility() const
{
    return m_accessibility;
}

GlobalShortcutBinder *ApplicationController::shortcutBinder(GlobalShortcutRole role) const
{
    switch (role) {
    case GlobalShortcutRole::Cancel:
        return m_cancelShortcutBinder;
    case GlobalShortcutRole::Pause:
        return m_pauseShortcutBinder;
    case GlobalShortcutRole::Dictation:
        break;
    }
    return m_shortcutBinder;
}

bool ApplicationController::globalShortcutsSupported(GlobalShortcutRole role) const
{
    return shortcutBinder(role)->supported();
}

bool ApplicationController::globalShortcutSupportKnown(GlobalShortcutRole role) const
{
    return shortcutBinder(role)->supportKnown();
}

bool ApplicationController::globalShortcutUsesDesktopChooser(GlobalShortcutRole role) const
{
    return shortcutBinder(role)->usesDesktopShortcutChooser();
}

ShortcutBinding ApplicationController::globalShortcut(GlobalShortcutRole role) const
{
    return shortcutBinder(role)->shortcut();
}

QString ApplicationController::globalShortcutDisplay(GlobalShortcutRole role) const
{
    return shortcutBinder(role)->shortcutDisplay();
}

bool ApplicationController::setGlobalShortcut(const ShortcutBinding &shortcut,
                                              QString *error,
                                              GlobalShortcutRole role)
{
#ifdef SPEECHER_E2E_HOOKS
    // E2E-build-only hook: a registration the desktop refuses, so the setup
    // assistant's shortcut gate can be seen holding on any runner.
    if (qEnvironmentVariableIntValue("SPEECHER_E2E_REFUSE_SHORTCUT") == 1) {
        if (error) {
            *error = QStringLiteral("Another application already owns %1").arg(shortcut.displayText());
        }
        return false;
    }
#endif
    for (const GlobalShortcutRole other : globalShortcutRoles) {
        if (other != role && !shortcut.isEmpty() && shortcut.overlaps(globalShortcut(other))) {
            if (error) {
                *error = shortcutTakenText(other);
            }
            return false;
        }
    }
    return shortcutBinder(role)->setShortcut(shortcut, error);
}

void ApplicationController::suspendGlobalShortcut()
{
    ++m_shortcutSuspensions;
    for (const GlobalShortcutRole role : globalShortcutRoles) {
        shortcutBinder(role)->suspend();
    }
    updateSessionShortcuts();
}

QString ApplicationController::resumeGlobalShortcut(GlobalShortcutRole *failedRole)
{
    m_shortcutSuspensions = std::max(0, m_shortcutSuspensions - 1);
    // A settings page shows one problem at a time: the first role's, in the
    // order the roles are listed, so the dictation shortcut's wins.
    QString firstError;
    for (const GlobalShortcutRole role : globalShortcutRoles) {
        const QString error = shortcutBinder(role)->resume();
        if (firstError.isEmpty() && !error.isEmpty()) {
            firstError = error;
            if (failedRole) {
                *failedRole = role;
            }
        }
    }
    updateSessionShortcuts();
    return firstError;
}

// The Cancel and Pause Shortcuts hold their keys only while their action
// applies; sessionKeysWanted decides Escape and Enter.
void ApplicationController::updateSessionShortcuts()
{
    if (!m_session) {
        return;
    }
    const DictationState state = m_session->reportedState();
    const ShortcutBinding cancelShortcut = globalShortcut(GlobalShortcutRole::Cancel);
    const SessionKeys wanted = sessionKeysWanted(state, m_session->reviewUp(), m_platform->escapeCancelsDictation(),
                                                 cancelShortcut,
                                                 globalShortcut(GlobalShortcutRole::Pause),
                                                 m_shortcutSuspensions > 0);
    bool escapeHeld = false;
    bool enterHeld = false;
    if (m_sessionKeyGrab) {
        escapeHeld = m_sessionKeyGrab->setGrabbed(SessionKeyGrab::Key::Escape, wanted.escape);
        enterHeld = m_sessionKeyGrab->setGrabbed(SessionKeyGrab::Key::Enter, wanted.enter);
    }
    m_cancelShortcutBinder->setArmed(dictationCancelable(state));
    m_pauseShortcutBinder->setArmed(dictationPausable(state));
    m_session->setReviewKeys(reviewKeysFor(escapeHeld, enterHeld, globalShortcutDisplay(GlobalShortcutRole::Cancel),
                                           globalShortcutDisplay()));
}

void ApplicationController::registerGlobalShortcut(GlobalShortcutRole role)
{
    shortcutBinder(role)->registerShortcut();
}

bool ApplicationController::removeGlobalShortcutRegistration(QString *error, GlobalShortcutRole role)
{
    return shortcutBinder(role)->removeRegistration(error);
}

IpcCommandHandler *ApplicationController::ipcCommands() const
{
    return m_ipcCommands;
}

void ApplicationController::showMainWindow()
{
    m_mainWindowByDefault = false;
    if (m_frontEnd) {
        m_frontEnd->showMainWindow();
    }
}

void ApplicationController::showDefaultMainWindow()
{
    showMainWindow();
    m_mainWindowByDefault = true;
    // Finder's open event lands within moments of launch; after this long a
    // file opened is a new request, and the window someone may be using stays.
    QTimer::singleShot(defaultMainWindowGraceMs, this, [this] { m_mainWindowByDefault = false; });
}

void ApplicationController::reopenMainWindowAfterUpdate()
{
    const bool reopen = m_settings->updatesReopenMainWindow();
    const qint64 age = QDateTime::currentMSecsSinceEpoch() - m_settings->updatesReopenMainWindowTime();
    m_settings->setUpdatesReopenMainWindow(false);
    if (reopen && age >= 0 && age < updateRelaunchMaxAgeMs) {
        QTimer::singleShot(0, this, &ApplicationController::showMain);
    }
}

void ApplicationController::showSettingsWindow()
{
    m_mainWindowByDefault = false;
    if (m_frontEnd) {
        m_frontEnd->showSettingsWindow();
    }
}

void ApplicationController::showSetupAssistant()
{
    showSetupAssistant(SetupAssistantPage::All);
}

void ApplicationController::showSetupAssistant(SetupAssistantPage page)
{
    if (m_frontEnd) {
        m_frontEnd->showSetupAssistant(page);
    }
}

void ApplicationController::showTranscribeFiles(const QStringList &paths)
{
    m_filesOpened = true;
    if (setupPending()) {
        m_pendingTranscribeFiles += paths;
        showSetupAssistant();
        return;
    }
    if (!m_frontEnd) {
        return;
    }
    if (std::exchange(m_mainWindowByDefault, false)) {
        m_frontEnd->hideMainWindow();
    }
    m_frontEnd->showTranscribeFiles(paths);
}

bool ApplicationController::filesOpened() const
{
    return m_filesOpened;
}

bool ApplicationController::heldFilesOpening() const
{
    return m_settings->dictationSnapshot().setupCompleted && !m_pendingTranscribeFiles.isEmpty();
}

// macOS answers the microphone grant asynchronously the first time, so a
// session start has to wait for the answer instead of capturing silence.
void ApplicationController::startWithMicrophone(std::function<void()> start)
{
    // Every session start funnels through here; a start dispatched during the
    // quit pump would re-pause the media quitApplication just resumed.
    if (m_quitting) {
        return;
    }
    if (m_fileTranscription->isRunning()) {
        if (m_frontEnd) {
            m_frontEnd->showDictationError(QStringLiteral(
                "Dictation is unavailable while files are being transcribed."));
        }
        return;
    }
#ifdef SPEECHER_E2E_HOOKS
    // E2E-build-only hook: stub runs have no microphone to ask about.
    if (qEnvironmentVariableIntValue("SPEECHER_E2E_SKIP_MIC_GATE") == 1) {
        start();
        return;
    }
#endif
    if (m_microphoneStartPending) {
        return;
    }
    const quint64 generation = ++m_microphoneStartGeneration;
    m_microphoneStartPending = true;
    m_platform->requestMicrophoneAccess(this, [this, generation, start = std::move(start)](bool granted) {
        if (generation != m_microphoneStartGeneration) {
            return;
        }
        m_microphoneStartPending = false;
        if (granted) {
            start();
        } else if (m_frontEnd) {
            m_frontEnd->showDictationError(microphoneAccessOffText(), {ErrorFix::MicrophonePermission});
        }
    });
}

bool ApplicationController::sessionActive() const
{
    const DictationState state = m_session->state();
    return state == DictationState::Starting || state == DictationState::Listening
        || state == DictationState::Paused || m_session->startPending() || m_microphoneStartPending;
}

void ApplicationController::toggle()
{
    if (sessionActive()) {
        stopListening();
        return;
    }
    if (!ensureSetupCompleted()) {
        return;
    }
    startWithMicrophone([this] { m_session->toggle(); });
}

void ApplicationController::toggleWith(const SessionOverrides &overrides)
{
    if (!ensureSetupCompleted()) {
        return;
    }
    if (sessionActive()) {
        stopListening();
        return;
    }
    startWithMicrophone([this, overrides] { m_session->toggleWith(overrides); });
}

void ApplicationController::startListening()
{
    startListeningWith({});
}

void ApplicationController::startListeningWith(const SessionOverrides &overrides)
{
    if (!ensureSetupCompleted()) {
        return;
    }
    startWithMicrophone([this, overrides] { m_session->startListeningWith(overrides); });
}

// A start still waiting for the push-to-talk delay or the microphone grant
// must not begin after the session it belongs to was stopped or cancelled.
void ApplicationController::dropPendingStart()
{
    m_shortcutGesture->dropDeferredStart();
    ++m_microphoneStartGeneration;
    m_microphoneStartPending = false;
}

void ApplicationController::stopListening()
{
    dropPendingStart();
    m_session->stopListening();
}

void ApplicationController::cancel()
{
    dropPendingStart();
    // The push-to-talk key that started the session may still be down; its
    // release must not act on a session that is already gone.
    m_shortcutGesture->forgetStartedSession();
    m_session->cancel();
}

void ApplicationController::showMain()
{
    if (!ensureSetupCompleted()) {
        return;
    }
    showMainWindow();
}

void ApplicationController::showSettings()
{
    if (!ensureSetupCompleted()) {
        return;
    }
    showSettingsWindow();
}

void ApplicationController::showSetup()
{
    showSetupAssistant();
}

void ApplicationController::quitApplication()
{
    // The bounded pump below dispatches timers and socket notifiers, so an
    // IPC "quit" arriving inside it re-enters here; once is enough.
    if (m_quitting) {
        return;
    }
    m_quitting = true;
    // Session teardown never resumes the media the session paused, so a quit
    // mid-dictation would leave the user's music paused. Cancel through
    // cancelForShutdown() — never stopListening(), whose Refining branch
    // delivers the fallback transcript into whatever window has focus — then
    // pump the event loop briefly: the media controllers resume players over
    // async D-Bus calls that would otherwise still be queued when the process
    // exits.
    dropPendingStart();
    if (m_session->state() != DictationState::Idle) {
        m_session->cancelForShutdown();
        QEventLoop resumeWindow;
        QTimer::singleShot(mediaResumeGraceMs, &resumeWindow, &QEventLoop::quit);
        resumeWindow.exec(QEventLoop::ExcludeUserInputEvents);
    }
    emit quitRequested();
}

// Children are otherwise deleted in creation order, which puts the provider
// registry (owner of the transcriber and refiner) before the session that still
// points at them. Tear the dependents down first.
ApplicationController::~ApplicationController()
{
    // Status watchers are written each change, and deleting the recording and
    // the session below makes some; cut the IPC off from them first.
    disconnect(m_session, nullptr, m_ipcCommands, nullptr);
    disconnect(m_recording, nullptr, m_ipcCommands, nullptr);
    // The gesture asks the session whether one is active; no binder may reach
    // it once the session is gone.
    disconnect(m_shortcutBinder, nullptr, m_shortcutGesture, nullptr);
    delete m_updates;
    m_updates = nullptr;
    delete m_recording;
    m_recording = nullptr;
    delete m_fileTranscription;
    m_fileTranscription = nullptr;
    delete m_session;
    m_session = nullptr;
}

void ApplicationController::completeSetup()
{
    m_settings->edit([](AppSettings &settings) {
        settings.setupCompleted = true;
        return true;
    });
    if (m_pendingTranscribeFiles.isEmpty()) {
        return;
    }
    // Queued, so the front end has closed its assistant and shown its window
    // before the Transcribe surface comes up over it.
    QTimer::singleShot(0, this, [this] {
        showTranscribeFiles(std::exchange(m_pendingTranscribeFiles, {}));
    });
}

void ApplicationController::setupAssistantClosed()
{
    if (!m_popupOnly && !heldFilesOpening()) {
        showMainWindow();
    }
}

bool ApplicationController::setupPending() const
{
    return m_frontEnd && !m_settings->dictationSnapshot().setupCompleted;
}

bool ApplicationController::ensureSetupCompleted()
{
    if (!setupPending()) {
        return true;
    }
    showSetupAssistant();
    return false;
}

} // namespace speecher
