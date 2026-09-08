#include "app/ApplicationController.h"

#include "app/AppFrontEnd.h"
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
#include "providers/AnthropicTranscriptRefiner.h"
#include "providers/ClaudeSpeechTranscriber.h"
#ifdef SPEECHER_E2E_HOOKS
#include "providers/E2EProviders.h"
#endif
#include "providers/CodexSpeechTranscriber.h"
#include "providers/OpenAiTranscriptRefiner.h"
#include "providers/ProviderRegistry.h"
#include "platform/GlobalShortcutBinder.h"

#include <QCoreApplication>
#include <QDateTime>
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

// A shortcut held longer than this is push-to-talk and ends with the key;
// anything shorter is a tap and stays a plain toggle.
constexpr qint64 pushToTalkHoldMs = 400;
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
        [platform = m_platform](bool enabled, QString *error) {
            return platform->setLaunchAtLogin(enabled, error);
        });
    m_settings->reconcileLaunchAtLogin();
    connect(m_shortcutBinder,
            &GlobalShortcutBinder::activated,
            this,
            &ApplicationController::handleShortcutPressed);
    connect(m_shortcutBinder,
            &GlobalShortcutBinder::deactivated,
            this,
            &ApplicationController::handleShortcutReleased);
    connect(m_shortcutBinder,
            &GlobalShortcutBinder::bindingChanged,
            this,
            &ApplicationController::globalShortcutChanged);
    connect(m_shortcutBinder,
            &GlobalShortcutBinder::supportChanged,
            this,
            &ApplicationController::globalShortcutSupportChanged);
    connect(m_shortcutBinder,
            &GlobalShortcutBinder::registrationFinished,
            this,
            &ApplicationController::globalShortcutRegistrationFinished);
    registerProviders();
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
                                     m_platform->createTextDelivery(targetProvider, this),
                                     m_providers,
                                     this);
    m_session->setScreenshotContextProvider(
        m_platform->createScreenshotContextProvider(this));
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

QKeySequence ApplicationController::globalShortcut() const
{
    return m_shortcutBinder->shortcut();
}

QString ApplicationController::globalShortcutDisplay() const
{
    return m_shortcutBinder->shortcutDisplay();
}

bool ApplicationController::setGlobalShortcut(const QKeySequence &shortcut, QString *error)
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

// macOS answers the microphone grant asynchronously the first time, so a
// session start has to wait for the answer instead of capturing silence.
void ApplicationController::startWithMicrophone(std::function<void()> start)
{
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

// Binders that report key release (macOS) drive both gestures from one binding:
// the press toggles, and a long enough hold ends the session it started.
void ApplicationController::handleShortcutPressed()
{
    m_shortcutPress.start();
    m_shortcutStartedSession = !sessionActive() && !m_microphoneStartPending;
    toggle();
}

void ApplicationController::handleShortcutReleased()
{
    if (!m_shortcutStartedSession) {
        return;
    }
    m_shortcutStartedSession = false;
    if ((sessionActive() || m_microphoneStartPending)
        && m_shortcutPress.elapsed() > pushToTalkHoldMs) {
        stopListening();
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
    emit quitRequested();
}

void ApplicationController::handleIpcCommand(const QString &command,
                                             const QString &outputFormat,
                                             QLocalSocket *socket)
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
    delete m_session;
    m_session = nullptr;
}

bool ApplicationController::ensureSetupCompleted()
{
    if (m_settings->setupCompleted()) {
        return true;
    }
    showSetupAssistant();
    return false;
}

// Model names and speeds here follow the September 2026 defaults; the speed
// and quality lines come from measured runs of the real refinement request
// (see .scratch/provider-stats/FINDINGS.md for the method and numbers). The
// score is the maintainers' overall ranking, folding those lines into one
// number out of 10.
static QVector<ProviderStat> refinementProviderStats(const QString &id)
{
    if (id == QStringLiteral("openai")) {
        return {{QStringLiteral("Score"), QStringLiteral("9 / 10")},
                {QStringLiteral("Default model"), QStringLiteral("gpt-5.6-luna")},
                {QStringLiteral("Speed"), QStringLiteral("About 3 seconds per dictation")},
                {QStringLiteral("Efficiency"), QStringLiteral("No reasoning pass; time varies run to run")},
                {QStringLiteral("Quality"), QStringLiteral("Excellent cleanup; applies spoken corrections reliably")}};
    }
    if (id == QStringLiteral("anthropic")) {
        return {{QStringLiteral("Score"), QStringLiteral("8 / 10")},
                {QStringLiteral("Default model"), QStringLiteral("Claude Sonnet 4.6")},
                {QStringLiteral("Speed"), QStringLiteral("About 2 seconds per dictation")},
                {QStringLiteral("Efficiency"), QStringLiteral("Light reasoning; very consistent finish times")},
                {QStringLiteral("Quality"), QStringLiteral("Excellent cleanup; can leave a spoken correction in")}};
    }
    return {};
}

void ApplicationController::registerProviders()
{
#ifdef SPEECHER_E2E_HOOKS
    // E2E-build-only hook: deterministic stub providers for the headless
    // dictation-panel flow runs. Never compiled into distributed builds.
    if (qEnvironmentVariableIntValue("SPEECHER_E2E_STUB") == 1) {
        // The stub stats mirror the real providers' shape (a Score line first)
        // so the setup-flow E2E can assert the rendering.
        m_providers->registerSpeechProvider(
            {QStringLiteral("e2e-stub"), QStringLiteral("E2E stub"), QString(), false, QString(),
             {{QStringLiteral("Score"), QStringLiteral("8 / 10")},
              {QStringLiteral("Engine"), QStringLiteral("Deterministic test stub")}}},
            createE2ESpeechTranscriber);
        m_providers->registerRefinementProvider(
            {QStringLiteral("e2e-stub"), QStringLiteral("E2E stub"), QString(), false, QString(),
             {{QStringLiteral("Score"), QStringLiteral("8 / 10")},
              {QStringLiteral("Engine"), QStringLiteral("Deterministic test stub")}}},
            createE2ETranscriptRefiner);
    }
#endif
    m_providers->registerSpeechProvider(
        {QStringLiteral("claude"),
         QStringLiteral("Claude Voice"),
         QStringLiteral("Sign in with Claude Code. If needed, run claude and use /login, then check again."),
         false,
         QStringLiteral("Deepgram Nova 3: words appear live as you speak. "
                        "About 60 languages, automatic punctuation and numerals."),
         {{QStringLiteral("Score"), QStringLiteral("8 / 10")},
          {QStringLiteral("Engine"), QStringLiteral("Deepgram Nova 3")},
          {QStringLiteral("Languages"), QStringLiteral("About 60")},
          {QStringLiteral("Speed"), QStringLiteral("Live stream; words appear as you speak")},
          {QStringLiteral("Accuracy"), QStringLiteral("Strong, holds up in noisy rooms")},
          {QStringLiteral("Formatting"), QStringLiteral("Automatic punctuation, capitals, numerals")}}},
        [](QObject *parent) {
            return new ClaudeSpeechTranscriber(parent);
        });
    m_providers->registerSpeechProvider(
        {QStringLiteral("codex"),
         QStringLiteral("ChatGPT Codex"),
         QStringLiteral("Sign in with ChatGPT using the ChatGPT app or Codex CLI, then check again."),
         false,
         QStringLiteral("GPT Live Transcribe: very accurate; text arrives a phrase "
                        "at a time after short pauses. Around 100 languages."),
         {{QStringLiteral("Score"), QStringLiteral("9 / 10")},
          {QStringLiteral("Engine"), QStringLiteral("GPT Live Transcribe")},
          {QStringLiteral("Languages"), QStringLiteral("Around 100")},
          {QStringLiteral("Speed"), QStringLiteral("A phrase at a time, after a short pause")},
          {QStringLiteral("Accuracy"), QStringLiteral("Excellent, even with accents and noise")},
          {QStringLiteral("Formatting"), QStringLiteral("Natural punctuation and phrasing")}}},
        [](QObject *parent) {
            return new CodexSpeechTranscriber(parent);
        });
    m_providers->registerRefinementProvider(
        {QStringLiteral("openai"), QStringLiteral("OpenAI"), QString(), true,
         QString(), refinementProviderStats(QStringLiteral("openai"))},
        [this](QObject *parent) { return new OpenAiTranscriptRefiner(m_secrets, parent); });
    m_providers->registerRefinementProvider(
        {QStringLiteral("anthropic"), QStringLiteral("Anthropic"), QString(), true,
         QString(), refinementProviderStats(QStringLiteral("anthropic"))},
        [](QObject *parent) { return new AnthropicTranscriptRefiner(parent); });
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
