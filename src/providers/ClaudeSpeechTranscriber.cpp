#include "providers/ClaudeSpeechTranscriber.h"

#include "providers/ClaudeCredentials.h"
#include "providers/ClaudeVoiceClient.h"

namespace speecher {

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

SpeechPrepareResult ClaudeSpeechTranscriber::prepare(const SpeechSettings &settings)
{
    const ClaudeCredentialResult credentials = ClaudeCredentials::load(settings.claudeCredentialsPath, true);
    if (!credentials.ok) {
        m_accessToken.clear();
        return {false, credentials.error};
    }
    m_accessToken = credentials.accessToken;
    return {true, QString()};
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
