#include "dictation/DictationSession.h"

#include "core/SettingsStore.h"
#include "core/TranscriptState.h"
#include "core/WordPreview.h"
#include "providers/ProviderRegistry.h"

#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QTimer>

namespace speecher {

DictationSession::DictationSession(SettingsStore *settings,
                                   AudioInput *audio,
                                   MediaController *mediaController,
                                   TextDeliveryAdapter *delivery,
                                   ProviderRegistry *providers,
                                   QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_audio(audio)
    , m_mediaController(mediaController)
    , m_delivery(delivery)
    , m_providers(providers)
    , m_transcript(new TranscriptState(this))
{
    connect(m_transcript, &TranscriptState::changed, this, [this](const QString &text) {
        const int words = m_settings ? m_settings->snapshot().ui.previewWords : 8;
        emit previewDisplayChanged(WordPreview::lastWords(text, words));
        qInfo() << "transcript changed length=" << text.size() << "previewWords=" << words;
        emit previewChanged(text);
    });
    connect(m_audio, &AudioInput::levelChanged, this, &DictationSession::audioLevelChanged);
    connect(m_audio, &AudioInput::audioChunk, this, [this](const QByteArray &pcm) {
        if (m_transcriber) {
            m_transcriber->sendAudio(pcm);
        }
    });
}

DictationState DictationSession::state() const
{
    return m_state;
}

QString DictationSession::stateName() const
{
    return dictationStateName(m_state);
}

QString DictationSession::lastMessage() const
{
    return m_lastMessage;
}

SessionResponse DictationSession::response(bool ok, const QString &message) const
{
    return {ok, stateName(), message.isEmpty() ? m_lastMessage : message};
}

void DictationSession::toggle()
{
    qInfo().noquote() << "toggle requested state=" + stateName();
    if (m_state == DictationState::Idle || m_state == DictationState::Error) {
        startListening();
    } else if (m_state == DictationState::Starting || m_state == DictationState::Listening) {
        stopListening();
    }
}

void DictationSession::startListening()
{
    if (m_state != DictationState::Idle && m_state != DictationState::Error) {
        return;
    }

    const AppSettings settings = m_settings->snapshot();
    QString providerError;
    if (!selectSpeechTranscriber(settings.speech.providerId, &providerError)) {
        setState(DictationState::Error, providerError);
        return;
    }
    if (settings.refinement.providerId != QStringLiteral("none")) {
        selectTranscriptRefiner(settings.refinement.providerId, nullptr);
    }

    ++m_generation;
    setState(DictationState::Starting);
    qInfo().noquote() << "startListening speechProvider=" + settings.speech.providerId
                      << "credentialsPath=" + settings.speech.claudeCredentialsPath
                      << "voiceBase=" + settings.speech.claudeEndpointBase;
    m_transcript->clear();
    m_refinedText.clear();
    emit previewDisplayChanged({});
    emit popupFrozenChanged(false);
    emit popupRefiningChanged(false);
    emit popupStatusChanged(QStringLiteral("Listening"));
    emit popupShowRequested();
    if (settings.ui.pauseMediaDuringTranscription) {
        m_mediaController->pausePlaying();
    }

    if (m_transcriber->requiresRefresh(settings.speech)) {
        emit popupOAuthRefreshRequested();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }

    const SpeechPrepareResult speechPrepared = m_transcriber->prepare(settings.speech);
    emit previewDisplayChanged({});
    if (!speechPrepared.ok) {
        qWarning().noquote() << "speech credentials unavailable message=" + speechPrepared.message;
        resumePausedMedia();
        setState(DictationState::Error, speechPrepared.message);
        return;
    }

    if (m_refiner && settings.refinement.providerId != QStringLiteral("none")
        && m_refiner->requiresRefresh(settings.refinement)) {
        emit popupOAuthRefreshRequested();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        m_refiner->refresh(settings.refinement);
        emit previewDisplayChanged({});
    }
    emit popupListeningIndicatorRequested();

    QString audioError;
    if (!m_audio->start(&audioError)) {
        qWarning().noquote() << "audio start failed message=" + audioError;
        resumePausedMedia();
        setState(DictationState::Error, audioError);
        return;
    }
    qInfo() << "audio capture started";
    m_transcriber->start(settings.speech);
    setState(DictationState::Listening);
}

void DictationSession::stopListening()
{
    if (m_state != DictationState::Starting && m_state != DictationState::Listening) {
        return;
    }
    const quint64 generation = m_generation;
    setState(DictationState::Stopping);
    qInfo() << "stopListening transcriptLength=" << m_transcript->text().size();
    m_audio->stop();
    if (m_transcriber) {
        m_transcriber->stop();
    }
    resumePausedMedia();
    QTimer::singleShot(650, this, [this, generation] {
        beginRefinement(generation);
    });
}

void DictationSession::setState(DictationState state, const QString &message)
{
    m_state = state;
    m_lastMessage = message;
    const QString label = dictationStateLabel(state, message);
    emit popupStatusChanged(label);
    qInfo().noquote() << "state changed state=" + stateName()
                      << "messagePresent=" + QString::number(!message.isEmpty());
    emit statusChanged(label);
}

void DictationSession::beginRefinement(quint64 generation)
{
    if (generation != m_generation || m_state != DictationState::Stopping) {
        qInfo() << "beginRefinement skipped stale generation";
        return;
    }
    if (m_transcript->isEmpty()) {
        qWarning() << "beginRefinement no transcript captured";
        setState(DictationState::Error, QStringLiteral("No transcript captured"));
        QTimer::singleShot(1400, this, [this, generation] {
            if (generation != m_generation || m_state != DictationState::Error) {
                return;
            }
            emit popupHideRequested();
            setState(DictationState::Idle);
        });
        return;
    }

    const AppSettings settings = m_settings->snapshot();
    if (settings.refinement.providerId == QStringLiteral("none")) {
        qInfo() << "refinement disabled delivering raw length=" << m_transcript->text().size();
        deliverFinal(m_transcript->text());
        return;
    }

    QString providerError;
    if (!selectTranscriptRefiner(settings.refinement.providerId, &providerError)) {
        qWarning().noquote() << "refinement provider unavailable message=" + providerError;
        m_lastMessage = providerError;
        deliverFinal(m_transcript->text());
        return;
    }

    const RefinementPrepareResult prepared = m_refiner->prepare(settings.refinement);
    if (!prepared.ok) {
        qWarning().noquote() << "refinement auth unavailable status=" + prepared.message;
        m_lastMessage = prepared.message;
        deliverFinal(m_transcript->text());
        return;
    }

    setState(DictationState::Refining);
    emit popupRefiningChanged(true);
    emit popupHidePreviewRequested();
    m_refinedText.clear();
    qInfo() << "refinement started provider=" << settings.refinement.providerId
            << "rawLength=" << m_transcript->text().size()
            << "vocabularyCount=" << settings.speech.vocabulary.size();
    m_refiner->refine(m_transcript->text(),
                      settings.speech.vocabulary,
                      settings.refinement);
}

void DictationSession::deliverFinal(const QString &text)
{
    const AppSettings settings = m_settings->snapshot();
    emit popupRefiningChanged(false);
    setState(DictationState::Delivering);
    qInfo() << "deliverFinal length=" << text.size();
    const DeliveryResult result = m_delivery->deliver(settings.output, text);
    emit popupHidePreviewRequested();
    if (result.ok) {
        const quint64 generation = m_generation;
        emit popupMessageRequested(result.message);
        emit statusChanged(result.message);
        QTimer::singleShot(1300, this, [this, generation] {
            if (generation != m_generation || m_state != DictationState::Delivering) {
                return;
            }
            emit popupHideRequested();
            setState(DictationState::Idle);
        });
    } else {
        emit popupFrozenChanged(false);
        qWarning().noquote() << "text delivery failed message=" + result.message;
        setState(DictationState::Error, result.message);
    }
}

void DictationSession::resumePausedMedia()
{
    m_mediaController->resumePaused();
}

bool DictationSession::selectSpeechTranscriber(const QString &providerId, QString *error)
{
    SpeechTranscriber *provider = m_providers->speechProvider(providerId);
    if (!provider) {
        if (error) {
            *error = QStringLiteral("Unknown speech provider: %1").arg(providerId);
        }
        return false;
    }
    if (provider != m_transcriber) {
        connectSpeechTranscriber(provider);
    }
    return true;
}

bool DictationSession::selectTranscriptRefiner(const QString &providerId, QString *error)
{
    TranscriptRefiner *provider = m_providers->refinementProvider(providerId);
    if (!provider) {
        if (error) {
            *error = QStringLiteral("Unknown refinement provider: %1").arg(providerId);
        }
        return false;
    }
    if (provider != m_refiner) {
        connectTranscriptRefiner(provider);
    }
    return true;
}

void DictationSession::connectSpeechTranscriber(SpeechTranscriber *transcriber)
{
    for (const QMetaObject::Connection &connection : m_transcriberConnections) {
        QObject::disconnect(connection);
    }
    m_transcriberConnections.clear();
    m_transcriber = transcriber;
    m_transcriberConnections << connect(m_transcriber, &SpeechTranscriber::partialTranscript, m_transcript, &TranscriptState::setPartial);
    m_transcriberConnections << connect(m_transcriber, &SpeechTranscriber::finalTranscript, m_transcript, &TranscriptState::commitFinal);
    m_transcriberConnections << connect(m_transcriber, &SpeechTranscriber::failed, this, [this](const QString &message) {
        qWarning().noquote() << "speech transcriber failed transcriptEmpty=" << m_transcript->isEmpty() << "message=" + message;
        if (m_transcript->isEmpty()) {
            const quint64 generation = m_generation;
            m_audio->stop();
            resumePausedMedia();
            setState(DictationState::Error, message);
            QTimer::singleShot(1800, this, [this, generation] {
                if (generation != m_generation || m_state != DictationState::Error) {
                    return;
                }
                emit popupHideRequested();
                setState(DictationState::Idle);
            });
        } else {
            m_lastMessage = message;
        }
    });
}

void DictationSession::connectTranscriptRefiner(TranscriptRefiner *refiner)
{
    for (const QMetaObject::Connection &connection : m_refinerConnections) {
        QObject::disconnect(connection);
    }
    m_refinerConnections.clear();
    m_refiner = refiner;
    m_refinerConnections << connect(m_refiner, &TranscriptRefiner::delta, this, [this](const QString &delta) {
        m_refinedText += delta;
        if (m_state != DictationState::Refining) {
            const int words = m_settings ? m_settings->snapshot().ui.previewWords : 8;
            emit previewDisplayChanged(WordPreview::lastWords(m_refinedText, words));
        }
    });
    m_refinerConnections << connect(m_refiner, &TranscriptRefiner::completed, this, [this](const QString &text) {
        deliverFinal(text.trimmed().isEmpty() ? m_transcript->text() : text.trimmed());
    });
    m_refinerConnections << connect(m_refiner, &TranscriptRefiner::failed, this, [this](const QString &message) {
        m_lastMessage = message;
        deliverFinal(m_transcript->text());
    });
}

} // namespace speecher
