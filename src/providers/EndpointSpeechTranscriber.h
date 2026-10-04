#pragma once

#include "dictation/DictationPorts.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>

class QHttpMultiPart;
class QNetworkReply;

namespace speecher {

// The multipart upload for one attempt's audio. The caller owns the parts.
struct SpeechEndpointUpload {
    QNetworkRequest request;
    QHttpMultiPart *parts = nullptr;
};

// prompt goes in the OpenAI `prompt` field, left out when empty, and
// spokenLanguage, a SpeechSettings::language, in `language`, left out for
// Automatic.
SpeechEndpointUpload speechEndpointUpload(const SpeechEndpointSettings &endpoint,
                                          const QByteArray &pcm16kMono,
                                          const QString &prompt,
                                          const QString &spokenLanguage);

// Transcription through the speech Custom Endpoint: an OpenAI-style
// POST {base}{path} with the whole attempt's audio once input finishes.
// Sends the audio once; a failure fails the attempt (rule A7).
class EndpointSpeechTranscriber final : public SpeechTranscriber {
    Q_OBJECT

public:
    // A batch server sends nothing while it transcribes, so the inactivity
    // limit allows a long recording on a slow machine; the deadline caps a
    // server that keeps trickling.
    explicit EndpointSpeechTranscriber(QObject *parent = nullptr,
                                       int inactivityTimeoutMs = 60000,
                                       int deadlineMs = 300000);

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
    void fail(quint64 attemptId, const ProviderFailure &failure);

    int m_inactivityTimeoutMs;
    int m_deadlineMs;
    QTimer m_inactivityTimer;
    QTimer m_deadlineTimer;
    QString m_timeoutReason;
    QString m_streamError;
    ProviderFailureKind m_streamErrorKind = ProviderFailureKind::Server;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    SpeechEndpointSettings m_endpoint;
    QString m_prompt;
    QString m_spokenLanguage;
    quint64 m_attemptId = 0;
    QByteArray m_pcm;
    QByteArray m_sseBuffer;
    QString m_streamedText;
    QString m_doneText;
    bool m_streaming = false;
};

} // namespace speecher
