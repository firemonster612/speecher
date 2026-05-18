#include "app/ApplicationController.h"

#include "core/AudioCapture.h"
#include "core/MediaPauseController.h"
#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#include "core/TranscriptState.h"
#include "core/WordPreview.h"
#include "output/TextDelivery.h"
#include "providers/ClaudeCredentials.h"
#include "providers/ClaudeVoiceClient.h"
#include "providers/OpenAiAuthProvider.h"
#include "providers/OpenAiRefiner.h"
#include "ui/MainWindow.h"
#include "ui/TranscriberPopup.h"

#include <QApplication>
#include <QDebug>
#include <QTimer>

namespace speecher {

ApplicationController::ApplicationController(bool popupOnly, QObject *parent)
    : QObject(parent)
    , m_popupOnly(popupOnly)
    , m_settings(new SettingsStore(this))
    , m_secrets(new SecretStore(m_settings, this))
    , m_transcript(new TranscriptState(this))
    , m_audio(new AudioCapture(this))
    , m_mediaPause(new MediaPauseController(this))
    , m_claude(new ClaudeVoiceClient(this))
    , m_refiner(new OpenAiRefiner(this))
    , m_delivery(new TextDelivery(this))
    , m_popup(new TranscriberPopup)
    , m_ipc(new SingleInstanceIpc(this))
{
    connect(m_ipc, &SingleInstanceIpc::commandReceived, this, &ApplicationController::handleIpcCommand);
    connect(m_transcript, &TranscriptState::changed, this, [this](const QString &text) {
        const QString preview = WordPreview::lastWords(text, m_settings->previewWords());
        m_popup->setPreview(preview);
        qInfo() << "transcript changed length=" << text.size() << "previewWords=" << m_settings->previewWords();
        emit previewChanged(text);
    });
    connect(m_audio, &AudioCapture::levelChanged, m_popup, &TranscriberPopup::setLevel);
    connect(m_audio, &AudioCapture::audioChunk, m_claude, &ClaudeVoiceClient::sendAudio);
    connect(m_claude, &ClaudeVoiceClient::partialTranscript, m_transcript, &TranscriptState::setPartial);
    connect(m_claude, &ClaudeVoiceClient::finalTranscript, m_transcript, &TranscriptState::commitFinal);
    connect(m_claude, &ClaudeVoiceClient::failed, this, [this](const QString &message) {
        qWarning().noquote() << "claude failed transcriptEmpty=" << m_transcript->isEmpty() << "message=" + message;
        if (m_transcript->isEmpty()) {
            const quint64 generation = m_generation;
            m_audio->stop();
            resumePausedMedia();
            setState(State::Error, message);
            QTimer::singleShot(1800, this, [this, generation] {
                if (generation != m_generation || m_state != State::Error) {
                    return;
                }
                m_popup->hide();
                setState(State::Idle);
            });
        } else {
            m_lastMessage = message;
        }
    });
    connect(m_refiner, &OpenAiRefiner::delta, this, [this](const QString &delta) {
        m_refinedText += delta;
        if (m_state != State::Refining) {
            m_popup->setPreview(WordPreview::lastWords(m_refinedText, m_settings->previewWords()));
        }
    });
    connect(m_refiner, &OpenAiRefiner::completed, this, [this](const QString &text) {
        deliverFinal(text.trimmed().isEmpty() ? m_transcript->text() : text.trimmed());
    });
    connect(m_refiner, &OpenAiRefiner::failed, this, [this](const QString &message) {
        m_lastMessage = message;
        deliverFinal(m_transcript->text());
    });
}

SettingsStore *ApplicationController::settings() const
{
    return m_settings;
}

SecretStore *ApplicationController::secretStore() const
{
    return m_secrets;
}

QString ApplicationController::stateName() const
{
    switch (m_state) {
    case State::Idle: return QStringLiteral("idle");
    case State::Starting: return QStringLiteral("starting");
    case State::Listening: return QStringLiteral("listening");
    case State::Stopping: return QStringLiteral("stopping");
    case State::Refining: return QStringLiteral("refining");
    case State::Delivering: return QStringLiteral("delivering");
    case State::Error: return QStringLiteral("error");
    }
    return QStringLiteral("error");
}

IpcResponse ApplicationController::response(bool ok, const QString &message) const
{
    return {ok, stateName(), message.isEmpty() ? m_lastMessage : message};
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
    qInfo().noquote() << "toggle requested state=" + stateName();
    if (m_state == State::Idle || m_state == State::Error) {
        startListening();
    } else if (m_state == State::Starting || m_state == State::Listening) {
        stopListening();
    }
}

void ApplicationController::startListening()
{
    if (m_state != State::Idle && m_state != State::Error) {
        return;
    }
    ++m_generation;
    setState(State::Starting);
    qInfo().noquote() << "startListening credentialsPath=" + m_settings->claudeCredentialsPath()
                      << "voiceUrl=" + claudeVoiceUrl().toString(QUrl::RemoveUserInfo);
    m_transcript->clear();
    m_refinedText.clear();
    m_popup->setPreview({});
    m_popup->setFrozen(false);
    m_popup->setRefining(false);
    m_popup->setStatus(QStringLiteral("Listening"));
    m_popup->showPopup();
    if (m_settings->pauseMediaDuringTranscription()) {
        m_mediaPause->pausePlaying();
    }

    const ClaudeCredentialResult credentials = ClaudeCredentials::load(m_settings->claudeCredentialsPath());
    if (!credentials.ok) {
        qWarning().noquote() << "claude credentials unavailable message=" + credentials.error;
        resumePausedMedia();
        setState(State::Error, credentials.error);
        return;
    }
    qInfo() << "claude credentials ok expiresAt=" << credentials.expiresAt.toString(Qt::ISODate)
            << "scopesCount=" << credentials.scopes.size()
            << "subscriptionTypePresent=" << !credentials.subscriptionType.isEmpty();

    QString audioError;
    if (!m_audio->start(&audioError)) {
        qWarning().noquote() << "audio start failed message=" + audioError;
        resumePausedMedia();
        setState(State::Error, audioError);
        return;
    }
    qInfo() << "audio capture started";
    m_claude->start(claudeVoiceUrl(), credentials.accessToken, m_settings->customVocabulary());
    setState(State::Listening);
}

void ApplicationController::stopListening()
{
    if (m_state != State::Starting && m_state != State::Listening) {
        return;
    }
    const quint64 generation = m_generation;
    setState(State::Stopping);
    qInfo() << "stopListening transcriptLength=" << m_transcript->text().size();
    m_audio->stop();
    m_claude->stop();
    resumePausedMedia();
    QTimer::singleShot(650, this, [this, generation] {
        beginRefinement(generation);
    });
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

void ApplicationController::setState(State state, const QString &message)
{
    m_state = state;
    m_lastMessage = message;
    const QString label = state == State::Error ? message : stateName().replace(0, 1, stateName().left(1).toUpper());
    m_popup->setStatus(label);
    qInfo().noquote() << "state changed state=" + stateName()
                      << "messagePresent=" + QString::number(!message.isEmpty());
    emit statusChanged(label);
}

void ApplicationController::beginRefinement(quint64 generation)
{
    if (generation != m_generation || m_state != State::Stopping) {
        qInfo() << "beginRefinement skipped stale generation";
        return;
    }
    if (m_transcript->isEmpty()) {
        qWarning() << "beginRefinement no transcript captured";
        setState(State::Error, QStringLiteral("No transcript captured"));
        QTimer::singleShot(1400, this, [this, generation] {
            if (generation != m_generation || m_state != State::Error) {
                return;
            }
            m_popup->hide();
            setState(State::Idle);
        });
        return;
    }
    if (m_settings->refinementProvider() == QStringLiteral("none")) {
        qInfo() << "refinement disabled delivering raw length=" << m_transcript->text().size();
        deliverFinal(m_transcript->text());
        return;
    }

    const OpenAiAuth auth = OpenAiAuthProvider(m_secrets, m_settings->openAiAuthMode()).resolve();
    if (!auth.ok) {
        qWarning().noquote() << "openai auth unavailable status=" + auth.status;
        m_lastMessage = auth.status;
        deliverFinal(m_transcript->text());
        return;
    }
    setState(State::Refining);
    m_popup->setRefining(true);
    m_popup->hidePreview();
    m_refinedText.clear();
    qInfo() << "openai refinement started rawLength=" << m_transcript->text().size()
            << "vocabularyCount=" << m_settings->customVocabulary().size();
    m_refiner->refine(m_transcript->text(),
                      m_settings->customVocabulary(),
                      auth.bearerToken,
                      auth.organization,
                      auth.project,
                      auth.endpointBase,
                      auth.accountId,
                      auth.chatgptBackend,
                      m_settings->openAiModel(),
                      m_settings->refinementStyle(),
                      m_settings->refinementOutputFormat());
}

void ApplicationController::deliverFinal(const QString &text)
{
    m_popup->setRefining(false);
    setState(State::Delivering);
    qInfo() << "deliverFinal length=" << text.size();
    const DeliveryResult result = m_delivery->deliver(m_settings->outputTypeCommand(), text, m_settings->fallbackClipboard());
    m_popup->hidePreview();
    if (result.ok) {
        const quint64 generation = m_generation;
        m_popup->showMessage(result.message);
        emit statusChanged(result.message);
        QTimer::singleShot(1300, this, [this, generation] {
            if (generation != m_generation || m_state != State::Delivering) {
                return;
            }
            m_popup->hide();
            setState(State::Idle);
        });
    } else {
        m_popup->setFrozen(false);
        qWarning().noquote() << "text delivery failed message=" + result.message;
        setState(State::Error, result.message);
    }
}

void ApplicationController::resumePausedMedia()
{
    m_mediaPause->resumePaused();
}

QUrl ApplicationController::claudeVoiceUrl() const
{
    QUrl base(m_settings->claudeEndpointBase());
    base.setScheme(base.scheme() == QStringLiteral("http") ? QStringLiteral("ws") : QStringLiteral("wss"));
    base.setPath(m_settings->claudeVoicePath());
    return base;
}

} // namespace speecher
