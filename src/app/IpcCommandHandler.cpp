#include "app/IpcCommandHandler.h"

#include "app/ApplicationController.h"
#include "app/ProvidersCommand.h"
#include "app/UpdateController.h"
#include "core/SettingsStore.h"
#include "dictation/DictationSession.h"
#include "recording/RecordingPresentation.h"
#include "recording/RecordingSession.h"

#include <QLocalSocket>
#ifdef SPEECHER_E2E_HOOKS
#include <QCoreApplication>
#include <QMetaEnum>
#endif

#include <utility>

namespace speecher {

IpcCommandHandler::IpcCommandHandler(ApplicationController &controller,
                                     RecordingSession &recording,
                                     std::shared_ptr<const PlatformComposition> platform,
                                     QObject *parent)
    : QObject(parent)
    , m_controller(controller)
    , m_recording(recording)
    , m_ipc(new SingleInstanceIpc(std::move(platform), this))
{
    DictationSession *session = m_controller.session();
    connect(&m_recording, &RecordingSession::stopped, this, [this](const RecordingStatus &status) {
        IpcResponse reply = response();
        reply.recording = status;
        for (const QPointer<QLocalSocket> &socket : std::exchange(m_recordStopReplies, {})) {
            SingleInstanceIpc::writeResponse(socket, reply);
        }
    });
    connect(m_ipc, &SingleInstanceIpc::requestReceived, this, &IpcCommandHandler::handle);
    connect(m_ipc, &SingleInstanceIpc::requestRefused, this, [this](const IpcResponse &refusal, QLocalSocket *socket) {
        IpcResponse reply = refusal;
        reply.state = response().state;
        SingleInstanceIpc::writeResponse(socket, reply);
    });
    // Not on each tick of the recording's duration, which every line carries.
    const auto publishStatus = [this] { m_ipc->publishStatus(statusResponse()); };
    connect(session, &DictationSession::stateChanged, this, publishStatus);
    connect(&m_recording, &RecordingSession::recordingChanged, this, publishStatus);
    connect(&m_recording, &RecordingSession::problemChanged, this, publishStatus);
}

IpcListenResult IpcCommandHandler::listen(QString *error)
{
    return m_ipc->listen(error);
}

IpcResponse IpcCommandHandler::response(bool ok, const QString &message) const
{
    const SessionResponse sessionResponse = m_controller.session()->response(ok, message);
    return {sessionResponse.ok, sessionResponse.state, sessionResponse.message};
}

IpcResponse IpcCommandHandler::statusResponse() const
{
    IpcResponse reply = response();
    reply.recording = m_recording.status();
    return reply;
}

void IpcCommandHandler::startRecording(const IpcRequest &request, QLocalSocket *socket)
{
    m_controller.startRecording(
        request.files.value(0), request.terms, request.microphoneOnly,
        [this, socket = QPointer(socket)](const QString &error) {
            if (!error.isEmpty()) {
                SingleInstanceIpc::writeResponse(socket, response(false, error));
                return;
            }
            // A command line that stopped waiting said the start failed, so
            // nothing may record behind its back.
            if (!socket || socket->state() != QLocalSocket::ConnectedState) {
                qWarning() << "record start's caller left before it answered; discarding the recording";
                m_recording.discard();
                return;
            }
            IpcResponse reply = response();
            reply.recording = m_recording.status();
            SettingsStore *settings = m_controller.settings();
            if (!settings->recordingConsentNoticeShown()) {
                settings->setRecordingConsentNoticeShown(true);
                reply.message = recordingConsentNotice();
            }
            SingleInstanceIpc::writeResponse(socket, reply);
        });
}

void IpcCommandHandler::handle(const IpcRequest &request, QLocalSocket *socket)
{
    SettingsStore *settings = m_controller.settings();
    const SessionOverrides &overrides = request.overrides;
    if (overrides.writingProfile
        && offeredWritingProfile(*overrides.writingProfile, settings->writingProfileSettings(), {}).isEmpty()) {
        SingleInstanceIpc::writeResponse(socket, response(false, QStringLiteral("Unknown writing profile")));
        return;
    }
    // toggle and start echo the overrides they read; see IpcResponse::writingProfile.
    const auto acknowledge = [this, socket, &overrides] {
        IpcResponse reply = response();
        reply.writingProfile = overrides.writingProfile.value_or(QString());
        reply.spokenLanguage = overrides.spokenLanguage.value_or(QString());
        SingleInstanceIpc::writeResponse(socket, reply);
    };
    switch (request.command) {
    case IpcCommand::Toggle:
        m_controller.toggleWith(overrides);
        acknowledge();
        return;
    case IpcCommand::Start:
        m_controller.startListeningWith(overrides);
        acknowledge();
        return;
    case IpcCommand::Stop:
        m_controller.stopListening();
        SingleInstanceIpc::writeResponse(socket, response());
        return;
    case IpcCommand::Cancel:
        m_controller.cancel();
        SingleInstanceIpc::writeResponse(socket, response());
        return;
    case IpcCommand::ShowMain:
        m_controller.showMain();
        SingleInstanceIpc::writeResponse(socket, response());
        return;
    case IpcCommand::ShowSettings:
        m_controller.showSettings();
        SingleInstanceIpc::writeResponse(socket, response());
        return;
    case IpcCommand::ShowSetup:
        m_controller.showSetup();
        SingleInstanceIpc::writeResponse(socket, response());
        return;
    case IpcCommand::Transcribe:
        m_controller.showTranscribeFiles(request.files);
        SingleInstanceIpc::writeResponse(socket, response());
        return;
#ifdef SPEECHER_E2E_HOOKS
    case IpcCommand::E2eUpdateCheck:
        m_controller.updates()->checkForUpdates(settings->updateChannel());
        SingleInstanceIpc::writeResponse(socket, response());
        return;
    case IpcCommand::E2eUpdateStatus: {
        UpdateController *updates = m_controller.updates();
        SingleInstanceIpc::writeResponse(socket, {
            true,
            QString::fromLatin1(QMetaEnum::fromType<UpdateController::State>().valueToKey(int(updates->state()))),
            QStringLiteral("currentVersion=%1\ncurrentBuild=%2\navailableVersion=%3\nerror=%4\nbannerVisible=%5\npid=%6")
                .arg(updates->currentVersion())
                .arg(SPEECHER_BUILD_NUMBER)
                .arg(updates->availableVersion(), updates->errorMessage(),
                     updates->bannerVisible() ? QStringLiteral("true") : QStringLiteral("false"))
                .arg(QCoreApplication::applicationPid()),
        });
        return;
    }
    case IpcCommand::E2ePause:
        m_controller.session()->pause();
        SingleInstanceIpc::writeResponse(socket, response());
        return;
    case IpcCommand::E2eResume:
        m_controller.session()->resume();
        SingleInstanceIpc::writeResponse(socket, response());
        return;
    case IpcCommand::E2eUpdateAccept:
        SingleInstanceIpc::writeResponse(socket, response());
        m_controller.updates()->installAndRestart();
        return;
#endif
    case IpcCommand::RecordStart:
        startRecording(request, socket);
        return;
    case IpcCommand::RecordStatus:
        SingleInstanceIpc::writeResponse(socket, statusResponse());
        return;
    case IpcCommand::RecordStop:
        if (!m_recording.isRecording()) {
            SingleInstanceIpc::writeResponse(socket, response(false, notRecordingText()));
            return;
        }
        m_recordStopReplies.append(socket);
        m_recording.stop();
        return;
    case IpcCommand::Status:
        SingleInstanceIpc::writeResponse(socket, statusResponse());
        return;
    case IpcCommand::WatchStatus:
        m_ipc->addStatusWatcher(socket, statusResponse());
        return;
    case IpcCommand::Last: {
        const QString lastTranscript = m_controller.lastTranscript();
        IpcResponse reply = response(!lastTranscript.isEmpty());
        reply.text = lastTranscript;
        SingleInstanceIpc::writeResponse(socket, reply);
        return;
    }
    case IpcCommand::Providers: {
        IpcResponse reply = response();
        reply.text = QString::fromUtf8(providerReportsJson(
            providerReports(*settings, *m_controller.localSetup(), *m_controller.providerRegistry())));
        SingleInstanceIpc::writeResponse(socket, reply);
        return;
    }
    case IpcCommand::AddVocabulary: {
        const std::optional<QStringList> held = settings->addVocabularyTerms(request.terms);
        IpcResponse reply = response(held.has_value());
        reply.skippedTerms = held.value_or(QStringList());
        SingleInstanceIpc::writeResponse(socket, reply);
        return;
    }
    case IpcCommand::Quit:
        SingleInstanceIpc::writeResponse(socket, response());
        m_controller.quitApplication();
        return;
    }
}

} // namespace speecher
