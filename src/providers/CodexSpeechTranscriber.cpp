#include "providers/CodexSpeechTranscriber.h"

#include "providers/CodexDictationClient.h"
#include "providers/OpenAiAuthProvider.h"

#include <memory>

namespace speecher {
namespace {

constexpr auto dictationEndpoint = "wss://chatgpt.com/backend-api/dictation/stream";

SpeechPrepareResult prepareCodex(QString *accessToken)
{
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

bool CodexSpeechTranscriber::requiresRefresh(const SpeechSettings &) const
{
    return OpenAiAuthProvider(nullptr, QStringLiteral("codex_oauth"))
        .requiresCodexOauthRefresh();
}

std::optional<SpeechPrepareJob> CodexSpeechTranscriber::createPrepareJob(
    const SpeechSettings &settings)
{
    auto accessToken = std::make_shared<QString>();
    SpeechPrepareJob job;
    job.showRefreshIndicator = requiresRefresh(settings);
    job.run = [accessToken] { return prepareCodex(accessToken.get()); };
    job.apply = [this, accessToken](const SpeechPrepareResult &result) {
        m_accessToken = result.ok ? *accessToken : QString();
    };
    return job;
}

SpeechPrepareResult CodexSpeechTranscriber::prepare(const SpeechSettings &)
{
    return prepareCodex(&m_accessToken);
}

void CodexSpeechTranscriber::startAttempt(quint64 attemptId,
                                          const SpeechSettings &)
{
    if (m_client) {
        m_client->cancel();
        m_client->deleteLater();
    }
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
                if (m_client == client && m_attemptId == attemptId) {
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
    client->start(QUrl(QString::fromLatin1(dictationEndpoint)), m_accessToken, 16000);
}

void CodexSpeechTranscriber::sendAudio(quint64 attemptId, const QByteArray &pcm)
{
    if (m_client && attemptId == m_attemptId) {
        m_client->sendAudio(pcm);
    }
}

void CodexSpeechTranscriber::finishInput(quint64 attemptId)
{
    if (m_client && attemptId == m_attemptId) {
        m_client->stop();
    }
}

void CodexSpeechTranscriber::cancelAttempt(quint64 attemptId)
{
    if (m_client && attemptId == m_attemptId) {
        m_client->cancel();
    }
}

} // namespace speecher
