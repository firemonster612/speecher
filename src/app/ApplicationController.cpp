#include "app/ApplicationController.h"

#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#include "dictation/DictationSession.h"
#include "platform/PlatformIntegration.h"
#include "providers/ClaudeSpeechTranscriber.h"
#include "providers/OpenAiTranscriptRefiner.h"
#include "providers/ProviderRegistry.h"
#include "ui/MainWindow.h"
#include "ui/TranscriberPopup.h"

#include <QApplication>
#include <QDebug>

namespace speecher {

ApplicationController::ApplicationController(bool popupOnly, QObject *parent)
    : QObject(parent)
    , m_popupOnly(popupOnly)
    , m_platform(PlatformFactory::create())
    , m_settings(new SettingsStore(this))
    , m_secrets(new SecretStore(m_settings, this))
    , m_providers(new ProviderRegistry(this))
    , m_popup(new TranscriberPopup(m_platform->createPopupPositioner(nullptr)))
    , m_ipc(new SingleInstanceIpc(m_platform, this))
{
    registerProviders();
    m_session = new DictationSession(m_settings,
                                     m_platform->createAudioInput(m_settings, this),
                                     m_platform->createMediaController(this),
                                     m_platform->createTextDelivery(this),
                                     m_providers,
                                     this);

    connect(m_ipc, &SingleInstanceIpc::commandReceived, this, &ApplicationController::handleIpcCommand);
    connect(m_session, &DictationSession::statusChanged, this, &ApplicationController::statusChanged);
    connect(m_session, &DictationSession::previewChanged, this, &ApplicationController::previewChanged);
    wireSessionToPopup();
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

const PlatformIntegration *ApplicationController::platform() const
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

bool ApplicationController::startIpc(QString *error)
{
    return m_ipc->listen(error);
}

void ApplicationController::showMainWindow()
{
    if (!m_mainWindow) {
        m_mainWindow = new MainWindow(this);
        connect(this, &ApplicationController::statusChanged, m_mainWindow, &MainWindow::setStatusText);
    }
    m_mainWindow->show();
    m_mainWindow->raise();
    m_mainWindow->activateWindow();
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

void ApplicationController::handleIpcCommand(const QString &command, QLocalSocket *socket)
{
    if (command == QStringLiteral("toggle")) {
        toggle();
        SingleInstanceIpc::writeResponse(socket, response());
    } else if (command == QStringLiteral("start")) {
        startListening();
        SingleInstanceIpc::writeResponse(socket, response());
    } else if (command == QStringLiteral("stop")) {
        stopListening();
        SingleInstanceIpc::writeResponse(socket, response());
    } else if (command == QStringLiteral("showMain")) {
        showMain();
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
}

void ApplicationController::wireSessionToPopup()
{
    connect(m_session, &DictationSession::previewDisplayChanged, m_popup, &TranscriberPopup::setPreview);
    connect(m_session, &DictationSession::audioLevelChanged, m_popup, &TranscriberPopup::setLevel);
    connect(m_session, &DictationSession::popupStatusChanged, m_popup, &TranscriberPopup::setStatus);
    connect(m_session, &DictationSession::popupShowRequested, m_popup, &TranscriberPopup::showPopup);
    connect(m_session, &DictationSession::popupHideRequested, m_popup, &TranscriberPopup::hide);
    connect(m_session, &DictationSession::popupHidePreviewRequested, m_popup, &TranscriberPopup::hidePreview);
    connect(m_session, &DictationSession::popupFrozenChanged, m_popup, &TranscriberPopup::setFrozen);
    connect(m_session, &DictationSession::popupRefiningChanged, m_popup, &TranscriberPopup::setRefining);
    connect(m_session, &DictationSession::popupOAuthRefreshRequested, m_popup, &TranscriberPopup::showOAuthRefreshIndicator);
    connect(m_session, &DictationSession::popupListeningIndicatorRequested, m_popup, &TranscriberPopup::showListeningIndicator);
    connect(m_session, &DictationSession::popupMessageRequested, m_popup, &TranscriberPopup::showMessage);
}

} // namespace speecher
