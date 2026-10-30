#include "providers/ClaudeSpeechTranscriber.h"

#include "providers/AnthropicAuthProvider.h"
#include "providers/ClaudeCredentials.h"
#include "providers/ClaudeVoiceClient.h"

#include <memory>

namespace speecher {

namespace {

SpeechPrepareResult loadClaudeAccessToken(const AccountSettings &accounts, QString *accessToken)
{
    const AnthropicAuth auth = AnthropicAuthProvider::forSpeech(accounts).resolve(true);
    // Warms the version cache the voice client reads as it connects, for
    // Claude Code's own sign-in only.
    if (accounts.anthropicAuthMode != QStringLiteral("cliproxy")) {
        ClaudeCredentials::installedVersion();
    }
    *accessToken = auth.accessToken;
    return {auth.ok, auth.error, auth.kind, auth.httpStatus};
}

} // namespace

ClaudeSpeechTranscriber::ClaudeSpeechTranscriber(QObject *parent)
    : SpeechTranscriber(parent)
{
}

bool ClaudeSpeechTranscriber::requiresRefresh(const SpeechSettings &, const AccountSettings &accounts) const
{
    return AnthropicAuthProvider::forSpeech(accounts).requiresRefresh();
}

std::optional<SpeechPrepareJob> ClaudeSpeechTranscriber::createPrepareJob(const SpeechSettings &settings,
                                                                          const AccountSettings &accounts)
{
    auto accessToken = std::make_shared<QString>();
    SpeechPrepareJob job;
    job.showRefreshIndicator = requiresRefresh(settings, accounts);
    job.run = [accounts, accessToken] {
        return loadClaudeAccessToken(accounts, accessToken.get());
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

SpeechPrepareResult ClaudeSpeechTranscriber::prepare(const SpeechSettings &, const AccountSettings &accounts)
{
    return loadClaudeAccessToken(accounts, &m_accessToken);
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
    connect(client, &ClaudeVoiceClient::connected, this, [this, client, attemptId] {
        if (m_client == client && m_attemptId == attemptId) {
            emit attemptConnected(attemptId);
        }
    });
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
    connect(client, &ClaudeVoiceClient::failed, this,
            [this, client, attemptId](const QString &message, bool retryable, SpeechPhase phase,
                                      ProviderFailureKind kind) {
        if (m_client == client && m_attemptId == attemptId) {
            emit failed({attemptId, message, retryable, phase, kind});
        }
    });
    m_client->start(voiceUrl(settings), m_accessToken, settings.vocabulary, settings.language);
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
