#include "providers/CodexSpeechTranscriber.h"

#include "providers/CliProxyCredentials.h"
#include "providers/CodexDictationClient.h"
#include "providers/OpenAiAuthProvider.h"

#include <QDataStream>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include <memory>

namespace speecher {
namespace {

constexpr int sampleRateHz = 16000;

QString dictationEndpoint()
{
    const QString override = qEnvironmentVariable("SPEECHER_CODEX_DICTATION_URL");
    return override.isEmpty()
        ? QStringLiteral("wss://chatgpt.com/backend-api/dictation/stream")
        : override;
}

QString transcribeEndpoint()
{
    const QString override = qEnvironmentVariable("SPEECHER_CODEX_TRANSCRIBE_URL");
    return override.isEmpty() ? QStringLiteral("https://chatgpt.com/backend-api/transcribe")
                              : override;
}

QByteArray wavFromPcm16Mono(const QByteArray &pcm, int rateHz)
{
    QByteArray wav;
    QDataStream stream(&wav, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + pcm.size());
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(1)
           << quint32(rateHz) << quint32(rateHz * 2) << quint16(2) << quint16(16);
    stream.writeRawData("data", 4);
    stream << quint32(pcm.size());
    stream.writeRawData(pcm.constData(), pcm.size());
    return wav;
}

SpeechPrepareResult prepareCodex(const SpeechSettings &settings, QString *accessToken)
{
    if (settings.codexAuthMode == QStringLiteral("cliproxy")) {
        const CliProxyCredentialResult credentials = CliProxyCredentials::loadWithRefresh(
            settings.cliproxyOauthDir, QStringLiteral("codex"), settings.codexCliproxyAccount);
        accessToken->clear();
        if (credentials.ok) {
            *accessToken = credentials.accessToken;
        }
        return {credentials.ok, credentials.error};
    }
    const OpenAiAuth auth = OpenAiAuthProvider(nullptr, QStringLiteral("codex_oauth")).resolve();
    if (!auth.ok) {
        accessToken->clear();
        return {false,
                QStringLiteral("%1. Sign in with ChatGPT in the ChatGPT app or Codex CLI.")
                    .arg(auth.status)};
    }
    *accessToken = auth.bearerToken;
    return {true, {}};
}

} // namespace

CodexSpeechTranscriber::CodexSpeechTranscriber(QObject *parent)
    : SpeechTranscriber(parent)
{
}

QString CodexSpeechTranscriber::id() const
{
    return QStringLiteral("codex");
}

QString CodexSpeechTranscriber::label() const
{
    return QStringLiteral("ChatGPT Codex");
}

bool CodexSpeechTranscriber::requiresRefresh(const SpeechSettings &settings) const
{
    if (settings.codexAuthMode == QStringLiteral("cliproxy")) {
        return CliProxyCredentials::accountNeedsRefresh(
            settings.cliproxyOauthDir, QStringLiteral("codex"), settings.codexCliproxyAccount);
    }
    return OpenAiAuthProvider(nullptr, QStringLiteral("codex_oauth"))
        .requiresCodexOauthRefresh();
}

std::optional<SpeechPrepareJob> CodexSpeechTranscriber::createPrepareJob(
    const SpeechSettings &settings)
{
    auto accessToken = std::make_shared<QString>();
    SpeechPrepareJob job;
    job.showRefreshIndicator = requiresRefresh(settings);
    job.run = [settings, accessToken] { return prepareCodex(settings, accessToken.get()); };
    job.apply = [this, accessToken](const SpeechPrepareResult &result) {
        m_accessToken = result.ok ? *accessToken : QString();
    };
    return job;
}

SpeechPrepareResult CodexSpeechTranscriber::prepare(const SpeechSettings &settings)
{
    return prepareCodex(settings, &m_accessToken);
}

void CodexSpeechTranscriber::startAttempt(quint64 attemptId,
                                          const SpeechSettings &settings)
{
    if (m_client) {
        m_client->cancel();
        m_client->deleteLater();
    }
    if (QNetworkReply *reply = m_retranscribeReply) {
        m_retranscribeReply.clear();
        reply->abort();
    }
    m_finalRetranscribe = settings.codexFinalRetranscribe;
    m_bufferedPcm.clear();
    m_attemptId = attemptId;
    m_client = new CodexDictationClient(this);
    CodexDictationClient *client = m_client;
    connect(client, &CodexDictationClient::partialTranscript,
            this, [this, client, attemptId](const QString &text) {
                if (m_client == client && m_attemptId == attemptId) {
                    emit partialTranscript(attemptId, text);
                }
            });
    connect(client, &CodexDictationClient::finalTranscript,
            this, [this, client, attemptId](const QString &text) {
                if (m_client == client && m_attemptId == attemptId) {
                    emit finalTranscript(attemptId, text);
                }
            });
    connect(client, &CodexDictationClient::completed,
            this, [this, client, attemptId] {
                if (m_client != client || m_attemptId != attemptId) {
                    return;
                }
                if (m_finalRetranscribe && !m_bufferedPcm.isEmpty()) {
                    startFinalRetranscribe(attemptId);
                } else {
                    emit attemptCompleted(attemptId);
                }
            });
    connect(client, &CodexDictationClient::failed,
            this, [this, client, attemptId](const QString &message,
                                            bool retryable,
                                            const QString &phase) {
                if (m_client == client && m_attemptId == attemptId) {
                    emit failed({attemptId, message, retryable, phase});
                }
            });
    client->start(QUrl(dictationEndpoint()), m_accessToken, sampleRateHz);
}

void CodexSpeechTranscriber::sendAudio(quint64 attemptId, const QByteArray &pcm)
{
    if (m_client && attemptId == m_attemptId) {
        if (m_finalRetranscribe) {
            m_bufferedPcm.append(pcm);
        }
        m_client->sendAudio(pcm);
    }
}

void CodexSpeechTranscriber::startFinalRetranscribe(quint64 attemptId)
{
    QNetworkRequest request{QUrl(transcribeEndpoint())};
    request.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QString::fromLatin1(codexBrowserUserAgent));
    request.setTransferTimeout(10000);

    auto *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("audio/wav"));
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"file\"; filename=\"dictation.wav\""));
    filePart.setBody(wavFromPcm16Mono(m_bufferedPcm, sampleRateHz));
    m_bufferedPcm.clear();
    multiPart->append(filePart);

    QNetworkReply *reply = m_network.post(request, multiPart);
    multiPart->setParent(reply);
    m_retranscribeReply = reply;
    // The transfer timeout only fires on inactivity; a response that trickles
    // in without finishing would stall dictation. This deadline aborts the
    // reply with m_retranscribeReply still set, which the finished handler
    // treats as a failure and falls back to the streamed transcript.
    QTimer::singleShot(15000, reply, [reply] { reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, attemptId] {
        reply->deleteLater();
        // A cleared pointer means the attempt was cancelled or superseded and
        // this reply was aborted deliberately; a timeout leaves it in place.
        if (m_attemptId != attemptId || m_retranscribeReply != reply) {
            return;
        }
        m_retranscribeReply.clear();
        // The streamed finals are already committed; a failed accuracy pass
        // falls back to them instead of failing the dictation.
        if (reply->error() != QNetworkReply::NoError) {
            qWarning("Codex final retranscribe failed, keeping the streamed transcript: %s",
                     qPrintable(reply->errorString()));
        } else {
            const QString text = QJsonDocument::fromJson(reply->readAll())
                                     .object().value(QStringLiteral("text")).toString().trimmed();
            if (!text.isEmpty()) {
                emit attemptTranscript(attemptId, text);
            }
        }
        emit attemptCompleted(attemptId);
    });
}

void CodexSpeechTranscriber::finishInput(quint64 attemptId)
{
    if (m_client && attemptId == m_attemptId) {
        m_client->stop();
    }
}

void CodexSpeechTranscriber::cancelAttempt(quint64 attemptId)
{
    if (attemptId != m_attemptId) {
        return;
    }
    if (m_client) {
        m_client->cancel();
    }
    if (QNetworkReply *reply = m_retranscribeReply) {
        m_retranscribeReply.clear();
        reply->abort();
    }
}

} // namespace speecher
