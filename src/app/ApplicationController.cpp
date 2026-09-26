#include "app/ApplicationController.h"

#include "app/AppFrontEnd.h"
#include "app/ProviderSetup.h"
#include "app/ShortcutSuspendingDelivery.h"
#include "app/UpdateController.h"
#ifdef Q_OS_MACOS
#include "app/MacSparkleUpdater.h"
#elif defined(Q_OS_WIN)
#include "app/WindowsInstallerUpdater.h"
#else
#include "app/AppImageUpdater.h"
#endif
#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#include "core/settings/SettingsSchema.h"
#include "dictation/DictationSession.h"
#include "providers/ProviderRegistry.h"
#include "platform/GlobalShortcutBinder.h"
#include "transcribe/FileTranscriptionSession.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#ifdef SPEECHER_E2E_HOOKS
#include <QMetaEnum>
#endif
#include <QTimer>
#ifdef Q_OS_MACOS
#include <QPermissions>
#endif

#include <utility>

namespace speecher {
namespace {

// Hybrid: a press toggles, and a hold longer than this is push-to-talk that
// ends with the key. 400 ms suited a two-hand chord; a one-finger tap on a
// single key lands and lifts in about 100 ms, and a deliberate hold is past
// 250 ms before the first word is out, so this sits clear of both gestures.
constexpr qint64 hybridHoldMs = 250;
// Push-to-talk: a press that lifts inside this window is a brush of the key,
// not a dictation, and must leave no trace.
constexpr int pushToTalkMisfireMs = 200;
// How long a quit mid-dictation waits for the asynchronous media-resume calls
// to leave the process before the event loop stops for good.
constexpr int mediaResumeGraceMs = 200;
#ifdef Q_OS_MACOS
constexpr int accessibilityPollMs = 5000;
#endif

} // namespace

ApplicationController::ApplicationController(bool popupOnly,
                                             std::shared_ptr<const PlatformComposition> platform,
                                             QObject *parent)
    : QObject(parent)
    , m_popupOnly(popupOnly)
    , m_platform(std::move(platform))
    , m_settings(new SettingsStore(this))
    , m_secrets(new SecretStore(m_settings, this))
    , m_providers(new ProviderRegistry(this))
    , m_shortcutBinder(m_platform->createGlobalShortcutBinder(this))
    , m_ipc(new SingleInstanceIpc(m_platform, this))
    , m_pushToTalkStart(new QTimer(this))
{
    const QString currentVersion = QStringLiteral(SPEECHER_VERSION);
    const qint64 currentBuildNumber = SPEECHER_BUILD_NUMBER;
    const QString previousVersion = m_settings->updatesLastRunVersion();
    const qint64 previousBuildNumber = m_settings->updatesLastRunBuildNumber();
    if (previousVersion != currentVersion || previousBuildNumber != currentBuildNumber) {
        const bool migratedNightlyUpgrade = previousBuildNumber < 0
            && !previousVersion.isEmpty()
            && previousVersion.contains(QStringLiteral("-nightly"))
            && currentVersion.contains(QStringLiteral("-nightly"))
            && previousVersion != currentVersion;
        const bool upgraded = previousBuildNumber >= 0
            ? currentBuildNumber > previousBuildNumber
            : compareBaseVersions(currentVersion, previousVersion) > 0
                || migratedNightlyUpgrade;
        if (upgraded
            && m_settings->updatesPendingWhatsNewVersion().isEmpty()) {
            m_settings->setUpdatesPendingWhatsNewVersion(previousVersion);
        }
        m_settings->setUpdatesLastRunVersion(currentVersion);
        m_settings->setUpdatesLastRunBuildNumber(currentBuildNumber);
    }
    m_pendingWhatsNewVersion = m_settings->updatesPendingWhatsNewVersion();
    m_settings->setLaunchAtLoginReconciler(
        [this](bool enabled, QString *error) {
            const bool accepted = m_platform->setLaunchAtLogin(enabled, error);
            if (accepted) {
                setLaunchAtLoginAccepted(true);
            }
            return accepted;
        });
    // The refusal has no window of its own: it becomes the caution the settings
    // surface draws beside the toggle, whenever that surface is next built.
    connect(m_settings,
            &SettingsStore::launchAtLoginReconciliationFailed,
            this,
            [this](const QString &) { setLaunchAtLoginAccepted(false); });
    m_settings->reconcileLaunchAtLogin();
    connect(m_shortcutBinder,
            &GlobalShortcutBinder::activated,
            this,
            &ApplicationController::handleShortcutPressed);
    connect(m_shortcutBinder,
            &GlobalShortcutBinder::deactivated,
            this,
            &ApplicationController::handleShortcutReleased);
    m_pushToTalkStart->setSingleShot(true);
    m_pushToTalkStart->setInterval(pushToTalkMisfireMs);
    connect(m_pushToTalkStart, &QTimer::timeout, this, &ApplicationController::startListening);
    connect(m_shortcutBinder, &GlobalShortcutBinder::bindingChanged, this, [this] {
        forgetShortcutGesture();
        emit globalShortcutChanged();
    });
    connect(m_shortcutBinder,
            &GlobalShortcutBinder::supportChanged,
            this,
            &ApplicationController::globalShortcutSupportChanged);
    connect(m_shortcutBinder,
            &GlobalShortcutBinder::registrationFinished,
            this,
            &ApplicationController::globalShortcutRegistrationFinished);
    registerProviders(*m_providers, m_secrets);
    TargetProvider *targetProvider = m_platform->createTargetProvider(this);
    targetProvider->setCorrectionObservationEnabled(m_settings->correctionLearningEnabled());
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
                if (m_settings->correctionLearningEnabled()) {
                    m_settings->recordCorrectionEvidence(
                        {original, corrected, confidence}, applicationId);
                }
            });
    m_audio = m_platform->createAudioInput(m_settings, this);
    m_session = new DictationSession(m_settings,
                                     m_audio,
                                     m_platform->createMediaController(this),
                                     targetProvider,
                                     new ShortcutSuspendingDelivery(
                                         m_platform->createTextDelivery(targetProvider, this),
                                         m_shortcutBinder,
                                         this),
                                     m_providers,
                                     this);
    m_session->setScreenshotContextProvider(
        m_platform->createScreenshotContextProvider(this));
    m_fileTranscription = new FileTranscriptionSession(m_settings, m_providers, this);
#ifdef Q_OS_MACOS
    m_updates = new MacSparkleUpdater(m_settings, m_session, this);
#elif defined(Q_OS_WIN)
    m_updates = new WindowsInstallerUpdater(m_settings, m_session, this);
#else
    m_updates = new AppImageUpdater(m_settings, m_session, this);
#endif

    connect(m_ipc, &SingleInstanceIpc::commandReceived, this, &ApplicationController::handleIpcCommand);
    connect(m_session, &DictationSession::stateChanged, this, &ApplicationController::stateChanged);
#ifdef Q_OS_MACOS
    connect(m_session, &DictationSession::stateChanged, this, [this](const QString &state) {
        if (state != QStringLiteral("Listening")) {
            return;
        }
        const std::optional<float> volume = m_platform->inputVolume();
        if (volume) {
            qInfo().noquote() << "macOS default input volume="
                              + QString::number(qRound(*volume * 100.0f)) + "%";
        }
    });
#endif
    connect(m_session, &DictationSession::statusChanged, this, &ApplicationController::statusChanged);
    connect(m_session, &DictationSession::previewChanged, this, &ApplicationController::previewChanged);
    connect(m_session, &DictationSession::transcriptDelivered, this, &ApplicationController::transcriptDelivered);
    connect(m_session, &DictationSession::audioLevelChanged, this, &ApplicationController::audioLevelChanged);
    connect(m_session, &DictationSession::statusChanged, this, [this](const QString &status) {
        if (m_settings->soundsEnabled()
            && (status == QStringLiteral("Listening")
                || status == QStringLiteral("Stopping"))) {
            if (m_frontEnd) {
                m_frontEnd->alert();
            }
        }
    });
#ifdef Q_OS_MACOS
    m_accessibilityPoll = new QTimer(this);
    m_accessibilityPoll->setInterval(accessibilityPollMs);
    connect(m_accessibilityPoll,
            &QTimer::timeout,
            this,
            &ApplicationController::refreshAccessibilityState);
#endif
    m_platform->watchAccessibilityChanges(this, [this] { refreshAccessibilityState(); });
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

bool ApplicationController::startFileTranscription(const QStringList &paths,
                                                   const TranscribeOptions &options,
                                                   QString *error)
{
    const DictationState state = m_session->state();
    const QString refusal = m_fileTranscription->isRunning()
        ? QStringLiteral("Files are already being transcribed.")
        : (state != DictationState::Idle && state != DictationState::Error) || m_microphoneStartPending
            ? QStringLiteral("Finish the dictation in progress, then transcribe the files.")
            : QString();
    if (!refusal.isEmpty()) {
        if (error) {
            *error = refusal;
        }
        return false;
    }
    return m_fileTranscription->start(paths, options);
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
    m_shortcutBinder->bind();
    const AccessibilityState state = m_platform->accessibilityState();
    const bool requestSucceeded = state.persistent && m_platform->requestAccessibility();
    m_accessibilitySupported = state.supported;
    m_accessibilityEnabled = state.enabled || requestSucceeded;
    m_accessibilityPersistent = state.persistent;
#ifdef Q_OS_MACOS
    if (m_accessibilityEnabled) {
        m_accessibilityPoll->stop();
    } else {
        m_accessibilityPoll->start();
    }
#endif
    emit accessibilityStateChanged(m_accessibilitySupported,
                                   m_accessibilityEnabled,
                                   m_accessibilityPersistent);
}

SettingsStore *ApplicationController::settings() const
{
    return m_settings;
}

UpdateController *ApplicationController::updates() const
{
    return m_updates;
}

QString ApplicationController::pendingWhatsNewVersion() const
{
    return m_pendingWhatsNewVersion;
}

void ApplicationController::clearPendingWhatsNew()
{
    if (m_pendingWhatsNewVersion.isEmpty()) {
        return;
    }
    m_pendingWhatsNewVersion.clear();
    m_settings->setUpdatesPendingWhatsNewVersion({});
    emit whatsNewChanged();
}

SecretStore *ApplicationController::secretStore() const
{
    return m_secrets;
}

ProviderRegistry *ApplicationController::providerRegistry() const
{
    return m_providers;
}

const PlatformComposition *ApplicationController::platform() const
{
    return m_platform.get();
}

QString ApplicationController::stateName() const
{
    return m_session->stateName();
}

IpcResponse ApplicationController::response(bool ok, const QString &message) const
{
    const SessionResponse sessionResponse = m_session->response(ok, message);
    return {sessionResponse.ok, sessionResponse.state, sessionResponse.message};
}

QString ApplicationController::outputSummary() const
{
    return m_platform->outputSummary();
}

bool ApplicationController::accessibilitySupported() const
{
    return m_accessibilitySupported;
}

bool ApplicationController::accessibilityEnabled() const
{
    return m_accessibilityEnabled;
}

bool ApplicationController::accessibilityPersistent() const
{
    return m_accessibilityPersistent;
}

bool ApplicationController::enableAccessibility(QString *error)
{
    const bool enabled = m_platform->enableAccessibilityPermanently(error);
    refreshAccessibilityState();
    return enabled;
}

bool ApplicationController::grabMainWindow(const QString &path) const
{
    return m_frontEnd && m_frontEnd->captureMainWindow(path);
}

bool ApplicationController::globalShortcutsSupported() const
{
    return m_shortcutBinder->supported();
}

bool ApplicationController::globalShortcutSupportKnown() const
{
    return m_shortcutBinder->supportKnown();
}

bool ApplicationController::globalShortcutUsesDesktopChooser() const
{
    return m_shortcutBinder->usesDesktopShortcutChooser();
}

QString ApplicationController::globalShortcutUnsupportedBindingReason(
    const ShortcutBinding &binding) const
{
    return m_shortcutBinder->unsupportedBindingReason(binding);
}

ShortcutBinding ApplicationController::globalShortcut() const
{
    return m_shortcutBinder->shortcut();
}

QString ApplicationController::globalShortcutDisplay() const
{
    return m_shortcutBinder->shortcutDisplay();
}

bool ApplicationController::setGlobalShortcut(const ShortcutBinding &shortcut, QString *error)
{
    return m_shortcutBinder->setShortcut(shortcut, error);
}

void ApplicationController::suspendGlobalShortcut()
{
    m_shortcutBinder->suspend();
}

QString ApplicationController::resumeGlobalShortcut()
{
    return m_shortcutBinder->resume();
}

void ApplicationController::registerGlobalShortcut()
{
    m_shortcutBinder->registerShortcut();
}

bool ApplicationController::removeGlobalShortcutRegistration(QString *error)
{
    return m_shortcutBinder->removeRegistration(error);
}

bool ApplicationController::startIpc(QString *error)
{
    return m_ipc->listen(error);
}

void ApplicationController::showMainWindow()
{
    if (m_frontEnd) {
        m_frontEnd->showMainWindow();
    }
}

void ApplicationController::showSettingsWindow()
{
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
    if (!m_settings->setupCompleted()) {
        m_pendingTranscribeFiles += paths;
        showSetupAssistant();
        return;
    }
    if (m_frontEnd) {
        m_frontEnd->showTranscribeFiles(paths);
    }
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
            m_frontEnd->showDictationError(QStringLiteral(
                "Microphone access is off. Allow Speecher under Privacy & Security > Microphone, then try again."));
        }
    });
}

bool ApplicationController::sessionActive() const
{
    const DictationState state = m_session->state();
    return state == DictationState::Starting || state == DictationState::Listening;
}

// One binding drives every activation mode; the mode decides what a press and
// a release do. The auto-repeat guard below applies to all of them.
void ApplicationController::handleShortcutPressed()
{
    // Key auto-repeat while held must not toggle again. A second press with no
    // release between it and the first is ambiguous: auto-repeat of a held key
    // (ignore) or a deliberate re-press on a desktop that never reports release
    // (toggle). The signals are identical, so the first gesture after launch
    // cannot satisfy both. We bias toward toggle until a release proves the
    // platform reports them: a wrongly swallowed press on a no-release desktop
    // could never be unstuck, whereas the mis-bias only costs the very first
    // hold, and only on a backend that streams auto-repeat as extra presses
    // (KGlobalAccel suppresses those at the binder; today's portal backends
    // fire once). handleShortcutReleased latches the guard on for good.
    if (m_shortcutDown && m_shortcutReleaseSeen) {
        return;
    }
    // That same unreleased second press is what proves the backend reports no
    // release, and with no release there is no "while held" to honour. From here
    // on push-to-talk takes the toggle path outright, which is the degradation
    // hybrid already gets by never reaching its release branch.
    if (m_shortcutDown && !m_shortcutPressedWhileDown) {
        m_shortcutPressedWhileDown = true;
        emit globalShortcutReleaseSupportChanged();
    }
    m_shortcutDown = true;
    m_shortcutPress.start();
    m_shortcutStartedSession = !sessionActive() && !m_microphoneStartPending;
    if (m_shortcutStartedSession && globalShortcutReportsRelease()
        && m_settings->shortcutActivationMode() == ShortcutActivationMode::PushToTalk) {
        // Deferred rather than started and cancelled: cancelling a Starting
        // session still opens the microphone and shows the popup for an
        // instant, which is a trace. The very first gesture on a desktop that
        // never reports release still lands here; its timer fires and the next
        // press ends the session as in every mode, so nothing wedges.
        m_pushToTalkStart->start();
        return;
    }
    toggle();
}

// Everything handleShortcutPressed learned applies to the binding and backend
// it learned it on. A new one may report releases where the old one did not,
// or the reverse, and a stale "held, release seen" latch would discard every
// press the new binding makes. No gesture can be in flight across a rebind, so
// the whole gesture state goes back to what it was at launch.
void ApplicationController::forgetShortcutGesture()
{
    m_pushToTalkStart->stop();
    m_shortcutDown = false;
    m_shortcutStartedSession = false;
    const bool reportedRelease = globalShortcutReportsRelease();
    m_shortcutReleaseSeen = false;
    m_shortcutPressedWhileDown = false;
    if (!reportedRelease) {
        emit globalShortcutReleaseSupportChanged();
    }
}

bool ApplicationController::globalShortcutReportsRelease() const
{
    return m_shortcutReleaseSeen || !m_shortcutPressedWhileDown;
}

bool ApplicationController::launchAtLoginAccepted() const
{
    return m_launchAtLoginAccepted;
}

void ApplicationController::setLaunchAtLoginAccepted(bool accepted)
{
    if (m_launchAtLoginAccepted == accepted) {
        return;
    }
    m_launchAtLoginAccepted = accepted;
    emit launchAtLoginAcceptedChanged();
}

void ApplicationController::handleShortcutReleased()
{
    m_shortcutDown = false;
    const bool firstRelease = !m_shortcutReleaseSeen;
    m_shortcutReleaseSeen = true;
    // A release settles the question for good, including against an auto-repeat
    // backend whose extra press looked like a missing one.
    if (firstRelease && m_shortcutPressedWhileDown) {
        emit globalShortcutReleaseSupportChanged();
    }
    if (!m_shortcutStartedSession) {
        return;
    }
    m_shortcutStartedSession = false;
    const bool starting = sessionActive() || m_microphoneStartPending;
    // Whatever the mode is now, a pending deferred start must not survive the
    // release: the mode can have changed since the press, and a session that
    // begins after the key is already up is push-to-talk's misfire, not a tap.
    const bool deferredStartPending = m_pushToTalkStart->isActive();
    m_pushToTalkStart->stop();
    switch (m_settings->shortcutActivationMode()) {
    case ShortcutActivationMode::Toggle:
        return;
    case ShortcutActivationMode::PushToTalk:
        if (!deferredStartPending && starting) {
            stopListening();
        }
        return;
    case ShortcutActivationMode::Hybrid:
        if (starting && m_shortcutPress.elapsed() > hybridHoldMs) {
            stopListening();
        }
        return;
    }
}

void ApplicationController::toggle()
{
    if (sessionActive() || m_microphoneStartPending) {
        stopListening();
        return;
    }
    if (!ensureSetupCompleted()) {
        return;
    }
    startWithMicrophone([this] { m_session->toggle(); });
}

void ApplicationController::startListening()
{
    if (!ensureSetupCompleted()) {
        return;
    }
    startWithMicrophone([this] { m_session->startListening(); });
}

void ApplicationController::stopListening()
{
    m_pushToTalkStart->stop();
    ++m_microphoneStartGeneration;
    m_microphoneStartPending = false;
    m_session->stopListening();
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
    if (m_session->state() != DictationState::Idle) {
        m_pushToTalkStart->stop();
        ++m_microphoneStartGeneration;
        m_microphoneStartPending = false;
        m_session->cancelForShutdown();
        QEventLoop resumeWindow;
        QTimer::singleShot(mediaResumeGraceMs, &resumeWindow, &QEventLoop::quit);
        resumeWindow.exec(QEventLoop::ExcludeUserInputEvents);
    }
    emit quitRequested();
}

void ApplicationController::handleIpcCommand(const QString &command,
                                             const QString &outputFormat,
                                             QLocalSocket *socket,
                                             const QStringList &files)
{
    const bool hasFormat = !outputFormat.isEmpty();
    if (hasFormat && outputFormat != QStringLiteral("plain") && outputFormat != QStringLiteral("html")) {
        SingleInstanceIpc::writeResponse(socket, response(false, QStringLiteral("Unknown output format")));
        return;
    }
    const OutputFormat format = outputFormatFromString(outputFormat);
    if (command == QStringLiteral("toggle")) {
        if (!ensureSetupCompleted()) {
            SingleInstanceIpc::writeResponse(socket, response());
            return;
        }
        if (sessionActive() || m_microphoneStartPending) {
            stopListening();
        } else {
            startWithMicrophone([this, hasFormat, format] {
                hasFormat ? m_session->toggleWithFormat(format) : m_session->toggle();
            });
        }
        SingleInstanceIpc::writeResponse(socket, response());
    } else if (command == QStringLiteral("start")) {
        if (!ensureSetupCompleted()) {
            SingleInstanceIpc::writeResponse(socket, response());
            return;
        }
        startWithMicrophone([this, hasFormat, format] {
            hasFormat ? m_session->startListeningWithFormat(format) : m_session->startListening();
        });
        SingleInstanceIpc::writeResponse(socket, response());
    } else if (command == QStringLiteral("stop")) {
        stopListening();
        SingleInstanceIpc::writeResponse(socket, response());
    } else if (command == QStringLiteral("showMain")) {
        showMain();
        SingleInstanceIpc::writeResponse(socket, response());
    } else if (command == QStringLiteral("showSettings")) {
        showSettings();
        SingleInstanceIpc::writeResponse(socket, response());
    } else if (command == QStringLiteral("showSetup")) {
        showSetup();
        SingleInstanceIpc::writeResponse(socket, response());
    } else if (command == QStringLiteral("transcribe")) {
        showTranscribeFiles(files);
        SingleInstanceIpc::writeResponse(socket, response());
    } else if (command == QStringLiteral("grab")) {
        // Screenshot seam for end-to-end runs: saves the main window into
        // SPEECHER_GRAB_DIR. The timestamp keeps a restarted process from
        // overwriting a frame the pre-restart process saved.
        const QString grabDir = qEnvironmentVariable("SPEECHER_GRAB_DIR");
        const bool saved = !grabDir.isEmpty()
            && grabMainWindow(QStringLiteral("%1/window-%2.png")
                                  .arg(grabDir)
                                  .arg(QDateTime::currentMSecsSinceEpoch()));
        SingleInstanceIpc::writeResponse(socket, response(saved));
#ifdef SPEECHER_E2E_HOOKS
    } else if (command == QStringLiteral("e2eUpdateCheck")) {
        m_updates->checkForUpdates(m_settings->updateChannel());
        SingleInstanceIpc::writeResponse(socket, response());
    } else if (command == QStringLiteral("e2eUpdateStatus")) {
        SingleInstanceIpc::writeResponse(socket, {
            true,
            QString::fromLatin1(QMetaEnum::fromType<UpdateController::State>().valueToKey(int(m_updates->state()))),
            QStringLiteral("currentVersion=%1\ncurrentBuild=%2\navailableVersion=%3\nerror=%4\nbannerVisible=%5\npid=%6")
                .arg(m_updates->currentVersion())
                .arg(SPEECHER_BUILD_NUMBER)
                .arg(m_updates->availableVersion(), m_updates->errorMessage(),
                     m_updates->bannerVisible() ? QStringLiteral("true") : QStringLiteral("false"))
                .arg(QCoreApplication::applicationPid()),
        });
    } else if (command == QStringLiteral("e2eUpdateAccept")) {
        SingleInstanceIpc::writeResponse(socket, response());
        m_updates->installAndRestart();
#endif
    } else if (command == QStringLiteral("status")) {
        SingleInstanceIpc::writeResponse(socket, response());
    } else if (command == QStringLiteral("quit")) {
        SingleInstanceIpc::writeResponse(socket, response());
        quitApplication();
    } else {
        SingleInstanceIpc::writeResponse(socket, response(false, QStringLiteral("Unknown command")));
    }
}

// Children are otherwise deleted in creation order, which puts the provider
// registry (owner of the transcriber and refiner) before the session that still
// points at them. Tear the dependents down first.
ApplicationController::~ApplicationController()
{
    delete m_updates;
    m_updates = nullptr;
    delete m_fileTranscription;
    m_fileTranscription = nullptr;
    delete m_session;
    m_session = nullptr;
}

void ApplicationController::completeSetup()
{
    m_settings->setSetupCompleted(true);
    if (m_pendingTranscribeFiles.isEmpty()) {
        return;
    }
    // Queued, so the front end has closed its assistant and shown its window
    // before the Transcribe surface comes up over it.
    QTimer::singleShot(0, this, [this] {
        showTranscribeFiles(std::exchange(m_pendingTranscribeFiles, {}));
    });
}

bool ApplicationController::ensureSetupCompleted()
{
    if (m_settings->setupCompleted()) {
        return true;
    }
    showSetupAssistant();
    return false;
}

void ApplicationController::refreshAccessibilityState()
{
    const AccessibilityState state = m_platform->accessibilityState();
    const bool changed = m_accessibilitySupported != state.supported
        || m_accessibilityEnabled != state.enabled
        || m_accessibilityPersistent != state.persistent;
    m_accessibilitySupported = state.supported;
    m_accessibilityEnabled = state.enabled;
    m_accessibilityPersistent = state.persistent;
#ifdef Q_OS_MACOS
    if (m_accessibilityEnabled) {
        m_accessibilityPoll->stop();
    } else if (!m_accessibilityPoll->isActive()) {
        m_accessibilityPoll->start();
    }
#endif
    if (!changed) {
        return;
    }
    emit accessibilityStateChanged(m_accessibilitySupported,
                                   m_accessibilityEnabled,
                                   m_accessibilityPersistent);
}

} // namespace speecher
