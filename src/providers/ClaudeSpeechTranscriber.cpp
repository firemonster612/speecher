#include "providers/ClaudeSpeechTranscriber.h"

#include "providers/ClaudeCredentials.h"
#include "providers/ClaudeVoiceClient.h"

#include <memory>

namespace speecher {

namespace {

SpeechPrepareResult loadClaudeAccessToken(const SpeechSettings &settings, QString *accessToken)
{
    const ClaudeCredentialResult credentials = ClaudeCredentials::load(settings.claudeCredentialsPath, true);
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
    , m_client(new ClaudeVoiceClient(this))
{
    connect(m_client, &ClaudeVoiceClient::partialTranscript, this, &ClaudeSpeechTranscriber::partialTranscript);
    connect(m_client, &ClaudeVoiceClient::finalTranscript, this, &ClaudeSpeechTranscriber::finalTranscript);
    connect(m_client, &ClaudeVoiceClient::failed, this, &ClaudeSpeechTranscriber::failed);
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

void ClaudeSpeechTranscriber::start(const SpeechSettings &settings)
{
    m_client->start(voiceUrl(settings), m_accessToken, settings.vocabulary);
}

void ClaudeSpeechTranscriber::sendAudio(const QByteArray &pcm)
{
    m_client->sendAudio(pcm);
}

void ClaudeSpeechTranscriber::stop()
{
    m_client->stop();
}

QUrl ClaudeSpeechTranscriber::voiceUrl(const SpeechSettings &settings) const
{
    QUrl base(settings.claudeEndpointBase);
    base.setScheme(base.scheme() == QStringLiteral("http") ? QStringLiteral("ws") : QStringLiteral("wss"));
    base.setPath(settings.claudeVoicePath);
    return base;
}

} // namespace speecher
