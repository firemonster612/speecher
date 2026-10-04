#include "providers/EndpointSpeechTranscriber.h"
#include "providers/EndpointRequest.h"

#include "core/VocabularyLimit.h"
#include "core/settings/SpokenLanguages.h"
#include "providers/PcmWav.h"
#include "providers/ServerSentEvents.h"

#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>

namespace speecher {
namespace {

constexpr int sampleRateHz = 16000;

QHttpPart formField(const QString &name, const QByteArray &value)
{
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QStringLiteral("form-data; name=\"%1\"").arg(name));
    part.setBody(value);
    return part;
}

QString endpointErrorMessage(const QByteArray &body, const QString &fallback)
{
    const QJsonObject object = QJsonDocument::fromJson(body).object();
    const QJsonValue error = object.value(QStringLiteral("error"));
    const QString message = error.isObject() ? error.toObject().value(QStringLiteral("message")).toString()
                                             : error.isString() ? error.toString() : object.value(QStringLiteral("message")).toString();
    return message.isEmpty() ? fallback : message;
}

} // namespace

SpeechEndpointUpload speechEndpointUpload(const SpeechEndpointSettings &endpoint,
                                          const QByteArray &pcm16kMono,
                                          const QString &prompt,
                                          const QString &spokenLanguage)
{
    QNetworkRequest request = endpointRequest(QUrl(endpoint.baseUrl + endpoint.path));
    if (!endpoint.apiKey.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + endpoint.apiKey.toUtf8());
    }
    auto *parts = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart file;
    file.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("audio/wav"));
    file.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QStringLiteral("form-data; name=\"file\"; filename=\"dictation.wav\""));
    file.setBody(wavFromPcm16Mono(pcm16kMono, sampleRateHz));
    parts->append(file);
    if (!endpoint.model.isEmpty()) {
        parts->append(formField(QStringLiteral("model"), endpoint.model.toUtf8()));
    }
    parts->append(formField(QStringLiteral("response_format"), "json"));
    if (const QString language = requestedSpokenLanguage(spokenLanguage); !language.isEmpty()) {
        parts->append(formField(QStringLiteral("language"), language.toUtf8()));
    }
    if (!prompt.isEmpty()) {
        parts->append(formField(QStringLiteral("prompt"), prompt.toUtf8()));
    }
    // Servers that stream answer with text/event-stream; the rest ignore it.
    parts->append(formField(QStringLiteral("stream"), "true"));
    return {request, parts};
}

EndpointSpeechTranscriber::EndpointSpeechTranscriber(QObject *parent,
                                                     int inactivityTimeoutMs,
                                                     int deadlineMs)
    : SpeechTranscriber(parent)
    , m_inactivityTimeoutMs(inactivityTimeoutMs)
    , m_deadlineMs(deadlineMs)
{
    m_inactivityTimer.setSingleShot(true);
    m_deadlineTimer.setSingleShot(true);
    // Aborting leaves m_reply set, so the finished handler reports a failure.
    const auto timeout = [this](const QString &reason) {
        if (m_reply) {
            m_timeoutReason = reason;
            m_reply->abort();
        }
    };
    connect(&m_inactivityTimer, &QTimer::timeout, this, [this, timeout] {
        timeout(QStringLiteral("the server sent nothing for %1 s").arg(m_inactivityTimeoutMs / 1000));
    });
    connect(&m_deadlineTimer, &QTimer::timeout, this, [this, timeout] {
        timeout(QStringLiteral("no complete answer within %1 s").arg(m_deadlineMs / 1000));
    });
}

QString EndpointSpeechTranscriber::id() const
{
    return QStringLiteral("endpoint");
}

QString EndpointSpeechTranscriber::label() const
{
    return QStringLiteral("Custom Endpoint");
}

bool EndpointSpeechTranscriber::requiresRefresh(const SpeechSettings &) const
{
    return false;
}

SpeechPrepareResult EndpointSpeechTranscriber::prepare(const SpeechSettings &settings)
{
    if (settings.endpoint.baseUrl.isEmpty()) {
        return {false, QStringLiteral("Set the speech endpoint's server URL in Settings.")};
    }
    return {true, {}};
}

void EndpointSpeechTranscriber::startAttempt(quint64 attemptId, const SpeechSettings &settings)
{
    cancelAttempt(m_attemptId);
    m_attemptId = attemptId;
    m_endpoint = settings.endpoint;
    m_spokenLanguage = settings.language;
    // The terms Claude Voice would get, in the same priority order.
    m_prompt = VocabularyLimit::limited(settings.vocabulary).join(QStringLiteral(", "));
    m_pcm.clear();
}

void EndpointSpeechTranscriber::sendAudio(quint64 attemptId, const QByteArray &pcm)
{
    if (attemptId == m_attemptId && !m_reply) {
        m_pcm += pcm;
    }
}

void EndpointSpeechTranscriber::finishInput(quint64 attemptId)
{
    if (attemptId != m_attemptId || m_reply) {
        return;
    }
    if (m_pcm.isEmpty()) {
        emit attemptCompleted(attemptId);
        return;
    }
    const SpeechEndpointUpload upload = speechEndpointUpload(m_endpoint, std::exchange(m_pcm, {}), m_prompt, m_spokenLanguage);
    m_sseBuffer.clear();
    m_streamedText.clear();
    m_doneText.clear();
    m_streaming = false;
    m_timeoutReason.clear();
    m_streamError.clear();
    QNetworkReply *reply = m_network.post(upload.request, upload.parts);
    upload.parts->setParent(reply);
    m_reply = reply;
    m_inactivityTimer.start(m_inactivityTimeoutMs);
    m_deadlineTimer.start(m_deadlineMs);
    connect(reply, &QNetworkReply::uploadProgress, this, [this, reply] {
        if (reply == m_reply) m_inactivityTimer.start(m_inactivityTimeoutMs);
    });
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        if (reply != m_reply) return;
        m_inactivityTimer.start(m_inactivityTimeoutMs);
        m_streaming = reply->header(QNetworkRequest::ContentTypeHeader).toString()
                          .startsWith(QStringLiteral("text/event-stream"));
        if (m_streaming) readStream();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, attemptId] { finishReply(reply, attemptId); });
}

// transcript.text.delta events carry pieces, transcript.text.done the whole.
// Speaches before 0.9 sends untyped {"text": …} events, one per segment.
void EndpointSpeechTranscriber::readStream()
{
    if (!m_streamError.isEmpty()) return;
    m_sseBuffer += m_reply->readAll();
    while (const std::optional<SseFrame> frame = takeSseFrame(m_sseBuffer)) {
        const QJsonObject event = QJsonDocument::fromJson(frame->data).object();
        const QString type = event.value(QStringLiteral("type")).toString(QString::fromUtf8(frame->name));
        if (type == QStringLiteral("error") || frame->name == "error") {
            m_streamError = endpointErrorMessage(frame->data, QStringLiteral("stream error"));
            // Avoid re-entering the reply's readyRead handler through abort().
            QMetaObject::invokeMethod(m_reply, &QNetworkReply::abort, Qt::QueuedConnection);
            return;
        }
        QString piece;
        if (type == QStringLiteral("transcript.text.delta")) {
            piece = event.value(QStringLiteral("delta")).toString();
        } else if (type.isEmpty()) {
            // These segments come trimmed, so they need a space between them.
            const QString segment = event.value(QStringLiteral("text")).toString().trimmed();
            piece = m_streamedText.isEmpty() || segment.isEmpty() ? segment : QLatin1Char(' ') + segment;
        } else if (type == QStringLiteral("transcript.text.done")) {
            m_doneText = event.value(QStringLiteral("text")).toString();
        }
        if (!piece.isEmpty()) {
            m_streamedText += piece;
            emit partialTranscript(m_attemptId, m_streamedText);
        }
    }
}

void EndpointSpeechTranscriber::finishReply(QNetworkReply *reply, quint64 attemptId)
{
    reply->deleteLater();
    if (reply != m_reply || attemptId != m_attemptId) {
        return;
    }
    if (m_streaming) readStream();
    m_reply.clear();
    m_inactivityTimer.stop();
    m_deadlineTimer.stop();
    const QByteArray body = m_streaming ? QByteArray() : reply->readAll();
    if (!m_streamError.isEmpty() || reply->error() != QNetworkReply::NoError) {
        const QString detail = !m_streamError.isEmpty() ? m_streamError : !m_timeoutReason.isEmpty()
            ? m_timeoutReason
            : endpointErrorMessage(body, reply->errorString());
        const QString message = QStringLiteral("Speech endpoint failed: %1").arg(detail);
        // Text the stream already produced is the dictation, cut short; the
        // audio is not sent again (rule A7), so keep what arrived.
        if (m_streaming && !m_streamedText.trimmed().isEmpty()) {
            qWarning().noquote() << message << "- keeping the text streamed so far";
            emit attemptTranscript(attemptId, m_streamedText.trimmed());
            fail(attemptId, message);
            return;
        }
        fail(attemptId, message);
        return;
    }
    const QString text = m_streaming
        ? (m_doneText.isEmpty() ? m_streamedText : m_doneText)
        : QJsonDocument::fromJson(body).object().value(QStringLiteral("text")).toString();
    const QString trimmed = text.trimmed();
    if (!trimmed.isEmpty()) {
        emit attemptTranscript(attemptId, trimmed);
    }
    emit attemptCompleted(attemptId);
}

void EndpointSpeechTranscriber::fail(quint64 attemptId, const QString &message)
{
    emit failed({attemptId, message, false, QStringLiteral("finalize")});
}

void EndpointSpeechTranscriber::cancelAttempt(quint64 attemptId)
{
    if (attemptId != m_attemptId) {
        return;
    }
    m_pcm.clear();
    m_inactivityTimer.stop();
    m_deadlineTimer.stop();
    if (QNetworkReply *reply = m_reply) {
        m_reply.clear();
        reply->abort();
    }
}

} // namespace speecher
