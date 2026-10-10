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
//
// A recording cuts the audio into utterances instead: each is posted on its
// own, after the one before has answered, with the end of the text before it
// in the prompt, and its text, empty when it had no words, is a final. The
// quiet between them is not sent, but for a short lead-in. One that fails in
// a way that may pass is reported and the next still goes; any other failure
// fails the attempt. No audio is sent twice and none overlaps.
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
    bool streamsFinalText(const SpeechSettings &settings) const override;
    int utteranceAnswerTimeoutMs() const override;
    void startAttempt(quint64 attemptId, const SpeechSettings &settings) override;
    void sendAudio(quint64 attemptId, const QByteArray &pcm) override;
    void finishInput(quint64 attemptId) override;
    void cancelAttempt(quint64 attemptId) override;
    void endUtterance(quint64 attemptId) override;
    void beginUtterance(quint64 attemptId) override;

private:
    void postAudio(const QByteArray &pcm, const QString &prompt);
    void closeUtterance();
    void uploadNextUtterance();
    void readStream();
    void finishReply(QNetworkReply *reply, quint64 attemptId);
    void finishUtterance(const QString &text);

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
    // The key terms, as each upload's prompt starts.
    QString m_vocabularyPrompt;
    QString m_spokenLanguage;
    quint64 m_attemptId = 0;
    bool m_inputFinished = false;
    // The open utterance's audio, or outside one the lead-in for the next.
    QByteArray m_pcm;
    // The caller marks the attempt's utterances, so its text comes an
    // utterance at a time; see SpeechSettings::cutIntoUtterances.
    bool m_cutIntoUtterances = false;
    // Between beginUtterance() and endUtterance().
    bool m_utteranceOpen = false;
    // Utterances waiting for the one uploading, oldest first.
    QList<QByteArray> m_utterances;
    // The end of the text the attempt's utterances have had so far.
    QString m_heardTail;
    QByteArray m_sseBuffer;
    QString m_streamedText;
    QString m_doneText;
    bool m_streaming = false;
};

} // namespace speecher
