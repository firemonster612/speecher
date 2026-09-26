#pragma once

#include "dictation/DictationPorts.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QPointer>

class QHttpMultiPart;
class QNetworkReply;

namespace speecher {

// The multipart upload for one attempt's audio. The caller owns the parts.
struct SpeechEndpointUpload {
    QNetworkRequest request;
    QHttpMultiPart *parts = nullptr;
};

SpeechEndpointUpload speechEndpointUpload(const SpeechEndpointSettings &endpoint, const QByteArray &pcm16kMono);

// Transcription through the speech Custom Endpoint: an OpenAI-style
// POST {base}{path} with the whole attempt's audio once input finishes.
// Sends the audio once; a failure fails the attempt (rule A7).
class EndpointSpeechTranscriber final : public SpeechTranscriber {
    Q_OBJECT

public:
    explicit EndpointSpeechTranscriber(QObject *parent = nullptr, int responseTimeoutMs = 60000);

    QString id() const override;
    QString label() const override;
    bool requiresRefresh(const SpeechSettings &settings) const override;
    SpeechPrepareResult prepare(const SpeechSettings &settings) override;
    void startAttempt(quint64 attemptId, const SpeechSettings &settings) override;
    void sendAudio(quint64 attemptId, const QByteArray &pcm) override;
    void finishInput(quint64 attemptId) override;
    void cancelAttempt(quint64 attemptId) override;

private:
    void readStream();
    void finishReply(QNetworkReply *reply, quint64 attemptId);
    void fail(quint64 attemptId, const QString &message);

    int m_responseTimeoutMs;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    SpeechEndpointSettings m_endpoint;
    quint64 m_attemptId = 0;
    QByteArray m_pcm;
    QByteArray m_sseBuffer;
    QString m_streamedText;
    QString m_doneText;
    bool m_streaming = false;
};

} // namespace speecher
