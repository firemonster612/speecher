#include "app/ApplicationController.h"
#include "app/LinuxComposition.h"

#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#include "dictation/DictationSession.h"
#include "providers/AnthropicTranscriptRefiner.h"
#include "providers/ClaudeSpeechTranscriber.h"
#include "providers/OpenAiTranscriptRefiner.h"
#include "providers/ProviderRegistry.h"
#include "ui/MainWindow.h"
#include "ui/SetupAssistant.h"
#include "ui/SettingsDialog.h"
#include "ui/TranscriberPopup.h"
#include "platform/atspi/AtSpiAccess.h"

#include <QApplication>
#include <QAction>
#include <QDebug>

#ifdef SPEECHER_WITH_KGLOBALACCEL
#include <KGlobalAccel>
#endif

namespace speecher {

ApplicationController::ApplicationController(bool popupOnly, QObject *parent)
    : QObject(parent)
    , m_popupOnly(popupOnly)
    , m_platform(linuxComposition())
    , m_settings(new SettingsStore(this))
    , m_secrets(new SecretStore(m_settings, this))
    , m_providers(new ProviderRegistry(this))
    , m_popup(new TranscriberPopup(m_platform->createPopupPositioner(nullptr)))
    , m_ipc(new SingleInstanceIpc(m_platform, this))
{
#ifdef SPEECHER_WITH_KGLOBALACCEL
    m_globalShortcutAction = new QAction(QStringLiteral("Toggle dictation"), this);
    m_globalShortcutAction->setObjectName(QStringLiteral("toggle-dictation"));
    m_globalShortcutAction->setProperty("componentName", QStringLiteral("local.speecher"));
    m_globalShortcutAction->setProperty("componentDisplayName", QStringLiteral("Speecher"));
    connect(m_globalShortcutAction, &QAction::triggered, this, &ApplicationController::toggle);
    const QKeySequence savedShortcut = globalShortcut();
    KGlobalAccel::self()->setDefaultShortcut(
        m_globalShortcutAction,
        {QKeySequence(Qt::META | Qt::ALT | Qt::Key_D)});
    if (!savedShortcut.isEmpty()
        && !KGlobalAccel::self()->setShortcut(
            m_globalShortcutAction,
            {savedShortcut},
            KGlobalAccel::Autoloading)) {
        qWarning() << "Could not restore the saved global shortcut";
    }
#endif
    const atspi::AccessibilityState initialAccessibility = atspi::accessibilityState();
    if (initialAccessibility.persistent) {
        atspi::requestAccessibility();
    }
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
    m_session = new DictationSession(m_settings,
                                     m_platform->createAudioInput(m_settings, this),
                                     m_platform->createMediaController(this),
                                     targetProvider,
                                     m_platform->createTextDelivery(targetProvider, this),
                                     m_providers,
                                     this);
    m_session->setScreenshotContextProvider(
        m_platform->createScreenshotContextProvider(this));

    connect(m_ipc, &SingleInstanceIpc::commandReceived, this, &ApplicationController::handleIpcCommand);
    connect(m_session, &DictationSession::statusChanged, this, &ApplicationController::statusChanged);
    connect(m_session, &DictationSession::previewChanged, this, &ApplicationController::previewChanged);
    connect(m_session, &DictationSession::statusChanged, this, [this](const QString &status) {
        if (m_settings->soundsEnabled()
            && (status == QStringLiteral("Listening")
                || status == QStringLiteral("Stopping"))) {
            QApplication::beep();
        }
    });
    wireSessionToPopup();
    if (initialAccessibility.persistent) {
        refreshAccessibilityState();
    } else {
        m_accessibilitySupported = initialAccessibility.supported;
        m_accessibilityEnabled = initialAccessibility.enabled;
        m_accessibilityPersistent = false;
        emit accessibilityStateChanged(m_accessibilitySupported,
                                       m_accessibilityEnabled,
                                       m_accessibilityPersistent);
    }
}

SettingsStore *ApplicationController::settings() const
{
    return m_settings;
}

SecretStore *ApplicationController::secretStore() const
{
    return m_secrets;
}

ProviderRegistry *ApplicationController::providerRegistry() const
{
    return m_providers;
}

const LinuxComposition *ApplicationController::platform() const
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
    const bool enabled = atspi::enableAccessibilityPermanently(error);
    refreshAccessibilityState();
    return enabled;
}

bool ApplicationController::globalShortcutsSupported() const
{
#ifdef SPEECHER_WITH_KGLOBALACCEL
    return qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(
        QStringLiteral("KDE"),
        Qt::CaseInsensitive);
#else
    return false;
#endif
}

QKeySequence ApplicationController::globalShortcut() const
{
#ifdef SPEECHER_WITH_KGLOBALACCEL
    const QList<QKeySequence> shortcuts = KGlobalAccel::self()->globalShortcut(
        QStringLiteral("local.speecher"),
        QStringLiteral("toggle-dictation"));
    return shortcuts.isEmpty() ? QKeySequence() : shortcuts.first();
#else
    return {};
#endif
}

bool ApplicationController::setGlobalShortcut(const QKeySequence &shortcut, QString *error)
{
#ifdef SPEECHER_WITH_KGLOBALACCEL
    if (shortcut.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Choose a key sequence");
        }
        return false;
    }
    if (!KGlobalAccel::self()->setShortcut(
            m_globalShortcutAction,
            {shortcut},
            KGlobalAccel::NoAutoloading)) {
        if (error) {
            *error = QStringLiteral("The desktop global-shortcut service rejected the key sequence");
        }
        return false;
    }
    return true;
#else
    Q_UNUSED(shortcut)
    if (error) {
        *error = QStringLiteral("KGlobalAccel is unavailable");
    }
    return false;
#endif
}

bool ApplicationController::startIpc(QString *error)
{
    return m_ipc->listen(error);
}

void ApplicationController::showMainWindow()
{
    if (!m_mainWindow) {
        m_mainWindow = new MainWindow(this);
        connect(this, &ApplicationController::statusChanged, m_mainWindow, &MainWindow::setStatusText);
        m_mainWindow->setStatusText(stateName());
    }
    m_mainWindow->show();
    m_mainWindow->raise();
    m_mainWindow->activateWindow();
}

void ApplicationController::showSettingsWindow()
{
    if (!m_settingsDialog) {
        m_settingsDialog = new SettingsDialog(this);
        m_settingsDialog->setAttribute(Qt::WA_DeleteOnClose);
    }
    m_settingsDialog->show();
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
}

void ApplicationController::showSetupAssistant()
{
    if (!m_setupAssistant) {
        m_setupAssistant = new SetupAssistant(this);
        m_setupAssistant->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_setupAssistant, &QDialog::accepted, this, [this] {
            if (!m_popupOnly) {
                showMainWindow();
            }
        });
    }
    m_setupAssistant->show();
    m_setupAssistant->raise();
    m_setupAssistant->activateWindow();
}

void ApplicationController::toggle()
{
    if (!ensureSetupCompleted()) {
        return;
    }
    m_session->toggle();
}

void ApplicationController::startListening()
{
    if (!ensureSetupCompleted()) {
        return;
    }
    m_session->startListening();
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
        hasFormat ? m_session->toggleWithFormat(format) : m_session->toggle();
        SingleInstanceIpc::writeResponse(socket, response());
    } else if (command == QStringLiteral("start")) {
        if (!ensureSetupCompleted()) {
            SingleInstanceIpc::writeResponse(socket, response());
            return;
        }
        hasFormat ? m_session->startListeningWithFormat(format) : m_session->startListening();
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
    m_providers->registerSpeechProvider({QStringLiteral("claude"), QStringLiteral("Claude Voice")}, [](QObject *parent) {
        return new ClaudeSpeechTranscriber(parent);
    });
    m_providers->registerRefinementProvider({QStringLiteral("openai"), QStringLiteral("OpenAI")}, [this](QObject *parent) {
        return new OpenAiTranscriptRefiner(m_secrets, parent);
    });
    m_providers->registerRefinementProvider({QStringLiteral("anthropic"), QStringLiteral("Anthropic")}, [](QObject *parent) {
        return new AnthropicTranscriptRefiner(parent);
    });
}

void ApplicationController::wireSessionToPopup()
{
    connect(m_session, &DictationSession::previewDisplayChanged, m_popup, &TranscriberPopup::setPreview);
    connect(m_session, &DictationSession::audioLevelChanged, m_popup, &TranscriberPopup::setLevel);
    connect(m_session, &DictationSession::popupStatusChanged, m_popup, &TranscriberPopup::setStatus);
    connect(m_session, &DictationSession::popupShowRequested, m_popup, &TranscriberPopup::showPopup);
    connect(m_session, &DictationSession::popupHideRequested, m_popup, &TranscriberPopup::hide);
    connect(m_session, &DictationSession::popupFrozenChanged, m_popup, &TranscriberPopup::setFrozen);
    connect(m_session, &DictationSession::popupRefiningChanged, m_popup, &TranscriberPopup::setRefining);
    connect(m_session, &DictationSession::popupOAuthRefreshRequested, m_popup, &TranscriberPopup::showOAuthRefreshIndicator);
    connect(m_session, &DictationSession::popupListeningIndicatorRequested, m_popup, &TranscriberPopup::showListeningIndicator);
    connect(m_session, &DictationSession::popupMessageRequested, m_popup, &TranscriberPopup::showMessage);
    connect(m_session, &DictationSession::popupErrorRequested, m_popup, &TranscriberPopup::showErrorMessage);
    connect(m_popup, &TranscriberPopup::errorDismissed, m_session, &DictationSession::stopListening);
    connect(m_popup, &TranscriberPopup::enableAccessibilityRequested, this, [this] {
        QString error;
        if (!enableAccessibility(&error)) {
            m_popup->showAccessibilityError(error);
        }
    });
    connect(this,
            &ApplicationController::accessibilityStateChanged,
            m_popup,
            &TranscriberPopup::setAccessibilityState);
}

void ApplicationController::refreshAccessibilityState()
{
    const atspi::AccessibilityState state = atspi::accessibilityState();
    m_accessibilitySupported = state.supported;
    m_accessibilityEnabled = state.enabled;
    m_accessibilityPersistent = state.persistent;
    emit accessibilityStateChanged(m_accessibilitySupported,
                                   m_accessibilityEnabled,
                                   m_accessibilityPersistent);
}

} // namespace speecher
