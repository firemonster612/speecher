#pragma once

#include "core/ProviderAttemptIssue.h"
#include "providers/SpeechChain.h"
#include "providers/SpeechTranscriber.h"

#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QPointer>

#include <functional>

namespace speecher {

class ProviderRegistry;
class TranscriptState;

// A session's speech attempts: the chain of speech providers it walks, the
// transcriber of the provider it is on, and the attempts it opens there.
// Each attempt has an id of its own, and only what the transcriber reports
// for the current one counts; the id moves on before an attempt is
// cancelled, so nothing a provider sends while it stops counts. The
// attempt's text goes into the session's transcript as it comes. What an
// ended or failed attempt means is the session's to decide, with the rules
// here: the reconnects a provider gets, when an ended attempt is a routine
// rollover, when nothing reached a service yet, and what to say when no
// provider could help.
class SpeechAttempts : public QObject {
    Q_OBJECT

public:
    // Where the transcriber for each provider the chain tries comes from.
    enum class Transcribers {
        // The registry's own, which outlives the session; letting it go
        // only disconnects it.
        Shared,
        // A fresh one for each provider, deleted once let go, so the session
        // never shares one with live dictation.
        Fresh,
    };

    SpeechAttempts(ProviderRegistry *providers,
                   Transcribers transcribers,
                   TranscriptState *transcript,
                   QObject *parent = nullptr);

    // Prepares each provider through acquire(); once one is ready() it has a
    // full set of reconnects and has not connected yet.
    SpeechChain *chain() const;
    // Lets go of the transcriber held, without cancelling its attempt, and
    // takes providerId's, connected; null, and none held, for an id the
    // registry does not offer.
    SpeechTranscriber *acquire(const QString &providerId);
    // Cancels the current attempt and lets the transcriber go.
    void release();
    // The transcriber held, or null.
    SpeechTranscriber *transcriber() const;
    // Whether text from the current attempt goes into the transcript now;
    // always, unless set.
    void setAcceptsText(std::function<bool()> acceptsText);

    quint64 currentId() const;
    // Opens the next attempt on the transcriber, with speech for the provider
    // the chain is on. The partial the last attempt left is committed first,
    // as nothing will finish it. False when the provider failed the attempt
    // from inside the call.
    bool startAttempt(const SpeechSettings &speech);
    // Cancels the current attempt; nothing it sends from here on counts.
    void cancel();
    // Nothing the current attempt sends from here on counts, without telling
    // the provider: for an attempt that already ended.
    void forgetAttempt();
    // To the current attempt. False when the provider ended it or failed
    // from inside the call.
    bool sendAudio(const QByteArray &pcm);
    void finishInput();
    void beginUtterance();
    void endUtterance();
    void commitPartial();

    // Whether an attempt of the provider the chain is on connected or sent
    // text.
    bool providerConnected() const;
    // Whether the current attempt connected or sent text, so the audio it
    // was sent reached the service.
    bool attemptConnected() const;
    // Whether the current attempt connected and streamed long enough to be
    // healthy.
    bool attemptWasHealthy() const;
    // Whether, when the current attempt failed so, nothing of the session's
    // audio had reached the provider's service: it is the provider's first
    // attempt, which never connected or sent text, and it was sent no audio
    // or failed while connecting.
    bool nothingReachedService(const SpeechFailure &failure) const;
    // Spends one of the provider's reconnects on a stream that dropped
    // (failure is retryable, while streaming): an attempt that streamed for
    // a while refills them first. False for any other failure, or once none
    // is left.
    bool takeReconnect(const SpeechFailure &failure);
    // For an attempt the provider ended before the input did: true when it
    // streamed long enough to be a routine rollover, which refills the
    // reconnects; false when its end is a refusal, endedTooSoon().
    bool takeRollover();
    SpeechFailure endedTooSoon() const;
    // Why no speech provider could take the audio: with fallbacks and
    // issues, each provider tried and why; otherwise providerMessage.
    QString failureText(const QString &providerMessage, const QList<ProviderAttemptIssue> &issues) const;

signals:
    // The current attempt's stream connected, or its first text came.
    void connected();
    // Text that will not change again went into the transcript: a final, or
    // a partial committed.
    void finalized(const QString &text);
    // When each part of the current attempt's text was spoken, timed from
    // the start of its audio.
    void segments(const QList<speecher::TranscriptSegment> &segments);
    void utteranceFailed(const QString &message);
    // The provider ended the current attempt.
    void completed();
    void failed(const speecher::SpeechFailure &failure);

private:
    void connectTranscriber();
    void disconnectTranscriber();
    void markConnected();
    bool takesText(quint64 attemptId);

    ProviderRegistry *m_providers;
    Transcribers m_transcribers;
    TranscriptState *m_transcript;
    SpeechChain *m_chain;
    SpeechAttemptPolicy m_policy;
    std::function<bool()> m_acceptsText;
    QPointer<SpeechTranscriber> m_transcriber;
    QList<QMetaObject::Connection> m_connections;
    quint64 m_id = 0;
    // Attempts opened on the provider since it was ready.
    int m_providerAttempts = 0;
    bool m_providerConnected = false;
    bool m_attemptConnected = false;
    bool m_attemptSentAudio = false;
};

} // namespace speecher
