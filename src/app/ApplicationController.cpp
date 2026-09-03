#include "app/ApplicationController.h"

#include "app/AppFrontEnd.h"
#include "app/UpdateController.h"
#ifdef Q_OS_MACOS
#include "app/MacSparkleUpdater.h"
#else
#include "app/AppImageUpdater.h"
#endif
#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#include "core/settings/SettingsSchema.h"
#include "dictation/DictationSession.h"
#include "providers/AnthropicTranscriptRefiner.h"
#include "providers/ClaudeSpeechTranscriber.h"
#include "providers/CodexSpeechTranscriber.h"
#include "providers/OpenAiTranscriptRefiner.h"
#include "providers/ProviderRegistry.h"
#include "platform/GlobalShortcutBinder.h"

#include <QCoreApplication>
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
    const QString previousVersion = m_settings->updatesLastRunVersion();
    if (previousVersion != currentVersion) {
        if (compareBaseVersions(currentVersion, previousVersion) > 0
            && m_settings->updatesPendingWhatsNewVersion().isEmpty()) {
            m_settings->setUpdatesPendingWhatsNewVersion(previousVersion);
        }
        m_settings->setUpdatesLastRunVersion(currentVersion);
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
    m_updates = new MacSparkleUpdater(m_settings, this);
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

QString ApplicationController::primaryOutputStatus() const
{
    return m_platform->primaryOutputStatus();
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

QKeySequence ApplicationController::globalShortcut() const
{
    return m_shortcutBinder->shortcut();
}

bool ApplicationController::setGlobalShortcut(const QKeySequence &shortcut, QString *error)
{
    return m_shortcutBinder->setShortcut(shortcut, error);
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
    if (m_frontEnd) {
        m_frontEnd->showSetupAssistant();
    }
}

// macOS answers the microphone grant asynchronously the first time, so a
// session start has to wait for the answer instead of capturing silence.
void ApplicationController::startWithMicrophone(std::function<void()> start)
{
#ifdef Q_OS_MACOS
    const auto refuse = [this] {
        if (m_frontEnd) {
            m_frontEnd->showDictationError(QStringLiteral(
                "Microphone access is off. Allow Speecher under Privacy & Security > Microphone, then try again."));
        }
    };
    switch (qApp->checkPermission(QMicrophonePermission{})) {
    case Qt::PermissionStatus::Denied:
        refuse();
        return;
    case Qt::PermissionStatus::Undetermined:
        qApp->requestPermission(QMicrophonePermission{}, this,
                                [start = std::move(start), refuse](const QPermission &permission) {
                                    permission.status() == Qt::PermissionStatus::Granted ? start() : refuse();
                                });
        return;
    case Qt::PermissionStatus::Granted:
        break;
    }
#endif
    start();
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
    const bool wasActive = sessionActive();
    toggle();
    m_shortcutStartedSession = !wasActive && sessionActive();
}

void ApplicationController::handleShortcutReleased()
{
    if (!m_shortcutStartedSession) {
        return;
    }
    m_shortcutStartedSession = false;
    if (sessionActive() && m_shortcutPress.elapsed() > pushToTalkHoldMs) {
        stopListening();
    }
}

void ApplicationController::toggle()
{
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
        startWithMicrophone([this, hasFormat, format] {
            hasFormat ? m_session->toggleWithFormat(format) : m_session->toggle();
        });
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
    } else if (command == QStringLiteral("status")) {
        SingleInstanceIpc::writeResponse(socket, response());
    } else {
        SingleInstanceIpc::writeResponse(socket, response(false, QStringLiteral("Unknown command")));
    }
}

bool ApplicationController::ensureSetupCompleted()
{
    if (m_settings->setupCompleted()) {
        return true;
    }
    showSetupAssistant();
    return false;
}

void ApplicationController::registerProviders()
{
    m_providers->registerSpeechProvider(
        {QStringLiteral("claude"),
         QStringLiteral("Claude Voice"),
         QStringLiteral("Sign in with Claude Code. If needed, run `claude` and use `/login`, then check again.")},
        [](QObject *parent) {
            return new ClaudeSpeechTranscriber(parent);
        });
    m_providers->registerSpeechProvider(
        {QStringLiteral("codex"),
         QStringLiteral("ChatGPT Codex"),
         QStringLiteral("Sign in with ChatGPT using the ChatGPT app (`/usr/bin/chatgpt`) or Codex CLI, then check again.")},
        [](QObject *parent) {
            return new CodexSpeechTranscriber(parent);
        });
    m_providers->registerRefinementProvider(
        {QStringLiteral("openai"), QStringLiteral("OpenAI"), QString(), true},
        [this](QObject *parent) { return new OpenAiTranscriptRefiner(m_secrets, parent); });
    m_providers->registerRefinementProvider(
        {QStringLiteral("anthropic"), QStringLiteral("Anthropic"), QString(), true},
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
