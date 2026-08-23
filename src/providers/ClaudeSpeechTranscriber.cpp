#include "providers/ClaudeSpeechTranscriber.h"

#include "providers/ClaudeCredentials.h"
#include "providers/ClaudeVoiceClient.h"

#include <memory>

namespace speecher {

namespace {

SpeechPrepareResult loadClaudeAccessToken(const SpeechSettings &settings, QString *accessToken)
{
    const ClaudeCredentialResult credentials = ClaudeCredentials::load(settings.claudeCredentialsPath, true);
    ClaudeCredentials::installedVersion();
    if (!credentials.ok) {
        if (accessToken) {
            accessToken->clear();
        }
        return {false, credentials.error};
    }
    if (accessToken) {
        *accessToken = credentials.accessToken;
    }
    return {true, QString()};
}

} // namespace

ClaudeSpeechTranscriber::ClaudeSpeechTranscriber(QObject *parent)
    : SpeechTranscriber(parent)
{
}

QString ClaudeSpeechTranscriber::id() const
{
    return QStringLiteral("claude");
}

QString ClaudeSpeechTranscriber::label() const
{
    return QStringLiteral("Claude Voice");
}

bool ClaudeSpeechTranscriber::requiresRefresh(const SpeechSettings &settings) const
{
    return ClaudeCredentials::requiresRefresh(settings.claudeCredentialsPath);
}

std::optional<SpeechPrepareJob> ClaudeSpeechTranscriber::createPrepareJob(const SpeechSettings &settings)
{
    auto accessToken = std::make_shared<QString>();
    SpeechPrepareJob job;
    job.showRefreshIndicator = requiresRefresh(settings);
    job.run = [settings, accessToken] {
        return loadClaudeAccessToken(settings, accessToken.get());
    };
    job.apply = [this, accessToken](const SpeechPrepareResult &result) {
        if (result.ok) {
            m_accessToken = *accessToken;
        } else {
            m_accessToken.clear();
        }
    };
    return job;
}

SpeechPrepareResult ClaudeSpeechTranscriber::prepare(const SpeechSettings &settings)
{
    return loadClaudeAccessToken(settings, &m_accessToken);
}

void ClaudeSpeechTranscriber::startAttempt(quint64 attemptId, const SpeechSettings &settings)
{
    createClient(attemptId, settings);
}

void ClaudeSpeechTranscriber::createClient(quint64 attemptId,
                                           const SpeechSettings &settings)
{
    if (m_client) {
        m_client->cancel();
        m_client->deleteLater();
    }
    m_attemptId = attemptId;
    m_client = new ClaudeVoiceClient(this);
    ClaudeVoiceClient *client = m_client;
    connect(client, &ClaudeVoiceClient::partialTranscript, this, [this, client, attemptId](const QString &text) {
        if (m_client == client && m_attemptId == attemptId) {
            emit partialTranscript(attemptId, text);
        }
    });
    connect(client, &ClaudeVoiceClient::finalTranscript, this, [this, client, attemptId](const QString &text) {
        if (m_client == client && m_attemptId == attemptId) {
            emit finalTranscript(attemptId, text);
        }
    });
    connect(client, &ClaudeVoiceClient::completed, this, [this, client, attemptId] {
        if (m_client == client && m_attemptId == attemptId) {
            emit attemptCompleted(attemptId);
        }
    });
    connect(client, &ClaudeVoiceClient::failed, this, [this, client, attemptId](const QString &message, bool retryable, const QString &phase) {
        if (m_client == client && m_attemptId == attemptId) {
            emit failed({attemptId, message, retryable, phase});
        }
    });
    m_client->start(voiceUrl(settings), m_accessToken, settings.vocabulary);
}

void ClaudeSpeechTranscriber::sendAudio(quint64 attemptId, const QByteArray &pcm)
{
    if (m_client && attemptId == m_attemptId) {
        m_client->sendAudio(pcm);
    }
}

void ClaudeSpeechTranscriber::finishInput(quint64 attemptId)
{
    if (m_client && attemptId == m_attemptId) {
        m_client->stop();
    }
}

void ClaudeSpeechTranscriber::cancelAttempt(quint64 attemptId)
{
    if (m_client && attemptId == m_attemptId) {
        m_client->cancel();
    }
}

QUrl ClaudeSpeechTranscriber::voiceUrl(const SpeechSettings &settings) const
{
    QUrl base(settings.claudeEndpointBase);
    base.setScheme(base.scheme() == QStringLiteral("http") ? QStringLiteral("ws") : QStringLiteral("wss"));
    base.setPath(settings.claudeVoicePath);
    return base;
}

} // namespace speecher
