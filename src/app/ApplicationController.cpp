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
#include "ui/AppWindow.h"
#include "ui/SettingsDialog.h"
#include "ui/TranscriberPopup.h"
#include "platform/atspi/AtSpiAccess.h"

#include <QApplication>
#include <QDebug>
#include <QTimer>

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
    connect(m_session, &DictationSession::audioLevelChanged, this, &ApplicationController::audioLevelChanged);
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

bool ApplicationController::grabMainWindow(const QString &path) const
{
    QWidget *window = m_appWindow ? static_cast<QWidget *>(m_appWindow)
                                  : static_cast<QWidget *>(m_mainWindow);
    return window && window->grab().save(path);
}

bool ApplicationController::startIpc(QString *error)
{
    return m_ipc->listen(error);
}

void ApplicationController::showMainWindow()
{
    const QString prototype = m_settings->uiPrototype();
    if (prototype != QStringLiteral("legacy")) {
        if (m_mainWindow) {
            m_mainWindow->hide();
            m_mainWindow->deleteLater();
            m_mainWindow = nullptr;
        }
        if (m_appWindow && m_appWindow->prototype() != prototype) {
            m_appWindow->flushPendingAutoSave();
            m_appWindow->rememberGeometry();
            m_appWindow->hide();
            m_appWindow->deleteLater();
            m_appWindow = nullptr;
        }
        if (!m_appWindow) {
            m_appWindow = new AppWindow(this, prototype);
        }
        m_appWindow->show();
        m_appWindow->raise();
        m_appWindow->activateWindow();
        return;
    }
    if (m_appWindow) {
        m_appWindow->flushPendingAutoSave();
        m_appWindow->rememberGeometry();
        m_appWindow->hide();
        m_appWindow->deleteLater();
        m_appWindow = nullptr;
    }
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
    if (m_settings->uiPrototype() != QStringLiteral("legacy")) {
        showMainWindow();
        m_appWindow->navigateToSettings();
        return;
    }
    if (!m_settingsDialog) {
        m_settingsDialog = new SettingsDialog(this);
        m_settingsDialog->setAttribute(Qt::WA_DeleteOnClose);
    }
    m_settingsDialog->show();
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
}

void ApplicationController::switchUiPrototype(const QString &prototype)
{
    if (prototype == m_settings->uiPrototype()) {
        return;
    }
    m_settings->setUiPrototype(prototype);
    if (m_appWindow) {
        m_appWindow->flushPendingAutoSave();
        m_appWindow->rememberGeometry();
        m_appWindow->hide();
        m_appWindow->deleteLater();
        m_appWindow = nullptr;
    }
    if (m_mainWindow) {
        m_mainWindow->hide();
        m_mainWindow->deleteLater();
        m_mainWindow = nullptr;
    }
    if (m_settingsDialog) {
        m_settingsDialog->close();
    }
    QTimer::singleShot(0, this, &ApplicationController::showMainWindow);
}

void ApplicationController::toggle()
{
    m_session->toggle();
}

void ApplicationController::startListening()
{
    m_session->startListening();
}

void ApplicationController::stopListening()
{
    m_session->stopListening();
}

void ApplicationController::showMain()
{
    showMainWindow();
}

void ApplicationController::showSettings()
{
    showSettingsWindow();
}

void ApplicationController::handleIpcCommand(const QString &command,
                                             const QString &outputFormat,
                                             const QString &uiPrototype,
                                             QLocalSocket *socket)
{
    const bool hasFormat = !outputFormat.isEmpty();
    if (hasFormat && outputFormat != QStringLiteral("plain") && outputFormat != QStringLiteral("html")) {
        SingleInstanceIpc::writeResponse(socket, response(false, QStringLiteral("Unknown output format")));
        return;
    }
    const OutputFormat format = outputFormatFromString(outputFormat);
    if (command == QStringLiteral("toggle")) {
        hasFormat ? m_session->toggleWithFormat(format) : toggle();
        SingleInstanceIpc::writeResponse(socket, response());
    } else if (command == QStringLiteral("start")) {
        hasFormat ? m_session->startListeningWithFormat(format) : startListening();
        SingleInstanceIpc::writeResponse(socket, response());
    } else if (command == QStringLiteral("stop")) {
        stopListening();
        SingleInstanceIpc::writeResponse(socket, response());
    } else if (command == QStringLiteral("showMain")) {
        if (!uiPrototype.isEmpty()) switchUiPrototype(uiPrototype);
        showMain();
        SingleInstanceIpc::writeResponse(socket, response());
    } else if (command == QStringLiteral("showSettings")) {
        if (!uiPrototype.isEmpty()) switchUiPrototype(uiPrototype);
        showSettings();
        SingleInstanceIpc::writeResponse(socket, response());
    } else if (command == QStringLiteral("status")) {
        SingleInstanceIpc::writeResponse(socket, response());
    } else {
        SingleInstanceIpc::writeResponse(socket, response(false, QStringLiteral("Unknown command")));
    }
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
