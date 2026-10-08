#include "providers/EndpointSpeechTranscriber.h"
#include "providers/EndpointRequest.h"

#include "core/VocabularyLimit.h"
#include "core/settings/SpokenLanguages.h"
#include "providers/PcmWav.h"
#include "providers/ProviderFailureClassification.h"
#include "providers/ServerSentEvents.h"

#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>

#include <algorithm>

namespace speecher {
namespace {

constexpr int sampleRateHz = 16000;
// How much of the text before an utterance its prompt carries: about a
// sentence, well inside the 224 tokens Whisper reads of a prompt.
constexpr qsizetype promptTailChars = 200;
// How much of the quiet before an utterance goes up with it, 300 ms, so a
// first sound softer than what counts as voice is not cut.
constexpr qsizetype leadInBytes = sampleRateHz * 2 * 3 / 10;

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

// Whether the first character with a script of its own, past punctuation and
// digits, is in a script written without spaces between words.
template <typename Iterator>
bool firstLetterWrittenWithoutSpaces(Iterator begin, Iterator end)
{
    const Iterator letter = std::find_if(begin, end, [](QChar c) { return c.script() > QChar::Script_Common; });
    if (letter == end) return false;
    switch (letter->script()) {
    case QChar::Script_Han:
    case QChar::Script_Hiragana:
    case QChar::Script_Katakana:
    case QChar::Script_Thai:
    case QChar::Script_Lao:
    case QChar::Script_Khmer:
    case QChar::Script_Myanmar:
    case QChar::Script_Tibetan:
        return true;
    default:
        return false;
    }
}

// A trimmed segment as it follows text: after a space, the way Whisper spaces
// segments, except next to a script written without spaces between words.
QString spacedSegment(const QString &text, const QString &segment)
{
    if (text.isEmpty() || segment.isEmpty() || firstLetterWrittenWithoutSpaces(text.rbegin(), text.rend())
        || firstLetterWrittenWithoutSpaces(segment.begin(), segment.end())) {
        return segment;
    }
    return QLatin1Char(' ') + segment;
}

SpeechFailure finalizeFailure(quint64 attemptId, const ProviderFailure &failure)
{
    return {attemptId, failure.message, false, QStringLiteral("finalize"), failure.kind, failure.httpStatus};
}

// The last promptTailChars of text, from the start of a word where it has
// spaces.
QString promptTail(const QString &text)
{
    if (text.size() <= promptTailChars) return text;
    const QString tail = text.right(promptTailChars);
    const qsizetype space = tail.indexOf(QLatin1Char(' '));
    return space < 0 ? tail : tail.mid(space + 1);
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
        return {false, QStringLiteral("Set the speech endpoint's server URL in Settings."),
                ProviderFailureKind::Unavailable};
    }
    return {true, {}};
}

bool EndpointSpeechTranscriber::streamsFinalText(const SpeechSettings &) const
{
    return true;
}

void EndpointSpeechTranscriber::startAttempt(quint64 attemptId, const SpeechSettings &settings)
{
    cancelAttempt(m_attemptId);
    m_attemptId = attemptId;
    m_endpoint = settings.endpoint;
    m_spokenLanguage = settings.language;
    // The terms Claude Voice would get, in the same priority order.
    m_vocabularyPrompt = VocabularyLimit::limited(settings.vocabulary).join(QStringLiteral(", "));
    m_inputFinished = false;
    m_pcm.clear();
    m_cutIntoUtterances = settings.cutIntoUtterances;
    m_utteranceOpen = false;
    m_utterances.clear();
    m_heardTail.clear();
    // There is no stream to open: the audio waits here for the upload, so
    // all of it reaches the server.
    emit attemptConnected(attemptId);
}

void EndpointSpeechTranscriber::sendAudio(quint64 attemptId, const QByteArray &pcm)
{
    if (attemptId != m_attemptId || m_inputFinished) {
        return;
    }
    m_pcm += pcm;
    if (m_cutIntoUtterances && !m_utteranceOpen && m_pcm.size() > leadInBytes) {
        m_pcm.remove(0, m_pcm.size() - leadInBytes);
    }
}

void EndpointSpeechTranscriber::finishInput(quint64 attemptId)
{
    if (attemptId != m_attemptId || m_inputFinished) {
        return;
    }
    m_inputFinished = true;
    if (m_cutIntoUtterances) {
        // An utterance still open goes up; the quiet outside one never does,
        // as a batch model writes words into silence.
        closeUtterance();
        m_pcm.clear();
        if (!m_reply) {
            uploadNextUtterance();
        }
        return;
    }
    if (m_pcm.isEmpty()) {
        emit attemptCompleted(attemptId);
        return;
    }
    postAudio(std::exchange(m_pcm, {}), m_vocabularyPrompt);
}

void EndpointSpeechTranscriber::beginUtterance(quint64 attemptId)
{
    if (attemptId == m_attemptId && m_cutIntoUtterances && !m_inputFinished) {
        m_utteranceOpen = true;
    }
}

void EndpointSpeechTranscriber::endUtterance(quint64 attemptId)
{
    if (attemptId != m_attemptId || !m_utteranceOpen) {
        return;
    }
    closeUtterance();
    if (!m_reply) {
        uploadNextUtterance();
    }
}

// The open utterance waits for its upload.
void EndpointSpeechTranscriber::closeUtterance()
{
    if (std::exchange(m_utteranceOpen, false) && !m_pcm.isEmpty()) {
        m_utterances.append(std::exchange(m_pcm, {}));
    }
}

// One upload at a time, so each prompt has the text before it and the
// finals come in order.
void EndpointSpeechTranscriber::uploadNextUtterance()
{
    if (m_utterances.isEmpty()) {
        if (m_inputFinished) {
            emit attemptCompleted(m_attemptId);
        }
        return;
    }
    // Whisper reads the end of a prompt, so the text before comes last.
    QStringList prompt{m_vocabularyPrompt, m_heardTail};
    prompt.removeAll(QString());
    postAudio(m_utterances.takeFirst(), prompt.join(QStringLiteral(". ")));
}

void EndpointSpeechTranscriber::postAudio(const QByteArray &pcm, const QString &prompt)
{
    const quint64 attemptId = m_attemptId;
    const SpeechEndpointUpload upload = speechEndpointUpload(m_endpoint, pcm, prompt, m_spokenLanguage);
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
            m_streamErrorKind = streamedErrorKind(event.value(QStringLiteral("error")).toObject());
            // Avoid re-entering the reply's readyRead handler through abort().
            QMetaObject::invokeMethod(m_reply, &QNetworkReply::abort, Qt::QueuedConnection);
            return;
        }
        QString piece;
        if (type == QStringLiteral("transcript.text.delta")) {
            piece = event.value(QStringLiteral("delta")).toString();
        } else if (type.isEmpty()) {
            piece = spacedSegment(m_streamedText, event.value(QStringLiteral("text")).toString().trimmed());
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
        const QString prefix = QStringLiteral("Speech endpoint failed: ");
        const ProviderFailure failure = !m_streamError.isEmpty()
            ? ProviderFailure{m_streamErrorKind, prefix + m_streamError,
                              reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()}
            : !m_timeoutReason.isEmpty()
            ? ProviderFailure{ProviderFailureKind::Timeout, prefix + m_timeoutReason}
            : replyFailure(*reply, prefix + endpointErrorMessage(body, reply->errorString()));
        // Text the stream already produced is the dictation, cut short; the
        // audio is not sent again (rule A7), so keep what arrived.
        const QString kept = m_streaming ? m_streamedText.trimmed() : QString();
        if (!kept.isEmpty()) {
            qWarning().noquote() << failure.message << "- keeping the text streamed so far";
        }
        if (m_cutIntoUtterances) {
            finishUtterance(kept);
            // A failure that may pass costs this utterance alone. Any other,
            // such as a refused key or a wrong path, would fail every one, so
            // it stops the stream.
            if (!isTransientFailure(failure.kind)) {
                emit failed(finalizeFailure(attemptId, failure));
                return;
            }
            emit utteranceFailed(finalizeFailure(attemptId, failure));
            uploadNextUtterance();
            return;
        }
        if (!kept.isEmpty()) {
            emit attemptTranscript(attemptId, kept);
        }
        emit failed(finalizeFailure(attemptId, failure));
        return;
    }
    const QString text = m_streaming
        ? (m_doneText.isEmpty() ? m_streamedText : m_doneText)
        : QJsonDocument::fromJson(body).object().value(QStringLiteral("text")).toString();
    const QString trimmed = text.trimmed();
    if (m_cutIntoUtterances) {
        finishUtterance(trimmed);
        uploadNextUtterance();
        return;
    }
    if (!trimmed.isEmpty()) {
        emit attemptTranscript(attemptId, trimmed);
    }
    emit attemptCompleted(attemptId);
}

void EndpointSpeechTranscriber::finishUtterance(const QString &text)
{
    if (text.isEmpty()) return;
    m_heardTail = promptTail(m_heardTail + spacedSegment(m_heardTail, text));
    emit finalTranscript(m_attemptId, text);
}

void EndpointSpeechTranscriber::cancelAttempt(quint64 attemptId)
{
    if (attemptId != m_attemptId) {
        return;
    }
    m_pcm.clear();
    m_utterances.clear();
    m_inactivityTimer.stop();
    m_deadlineTimer.stop();
    if (QNetworkReply *reply = m_reply) {
        m_reply.clear();
        reply->abort();
    }
}

} // namespace speecher
