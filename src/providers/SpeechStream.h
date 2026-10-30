#pragma once

#include "core/AppSettings.h"
#include "providers/SpeechAttempts.h"

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QTimer>

#include <functional>

namespace speecher {

class ProviderRegistry;
class TranscriptState;

// One input's audio, kept as it is heard and streamed to a session's speech
// providers, each with a fresh transcriber. Audio heard while a provider
// prepares waits for it, and each provider takes the input from the oldest
// audio kept; one that fails before any of it reached its service hands all
// of it to the next. After, the input stays with that provider: a stream
// that drops reopens within its reconnects, and one that ends healthy rolls
// over, each picking up at the next unsent chunk, and once a stream has
// connected the audio it was sent is let go. The input ends in finished(),
// or failed().
class SpeechStream : public QObject {
    Q_OBJECT

public:
    // The owner's own way back from a failure of the current attempt, tried
    // before the stream's: true when it took the failure.
    using Recovery = std::function<bool(const SpeechFailure &failure)>;

    // Every attempt the stream starts asks for options.
    SpeechStream(ProviderRegistry *providers, TranscriptState *transcript, SpeechAttemptOptions options,
                 QObject *parent = nullptr);

    SpeechAttempts *attempts() const;
    // The transcriber of the provider the stream is on, or null.
    SpeechTranscriber *transcriber() const;
    // Renews that provider's sign-in, with the stream's settings, for its
    // next attempt; ends in the chain's signInRenewed().
    void renewSignIn();
    void setRecovery(Recovery recovery);
    // How the stream goes on once a provider ends a healthy attempt before
    // the input ended; by default the next attempt opens at once.
    void setRollover(std::function<void()> rollover);
    // The most audio kept for a stream that cannot take it; past it the
    // oldest goes, with audioLost(). No limit unless set.
    void setMostKeptBytes(qsizetype bytes);

    // Ready for a new input: no audio, utterance marks, text or providers'
    // issues.
    void reset();
    // Walks the speech chain from settings' speech provider; the first that
    // prepares takes the input. A primary that cannot listen for the Spoken
    // Language fails the stream.
    void prepare(const AppSettings &settings);
    // The settings the stream runs with, with the keys the chain read.
    const AppSettings &settings() const;
    // settings()' speech, for the provider the chain is on.
    SpeechSettings speechSettings() const;

    void append(const QByteArray &pcm);
    // Whether any audio was appended since reset().
    bool hasAudio() const;
    // The input heard all it will; once it is all sent the provider is told.
    void endInput();
    // Has the provider begin an utterance with the last voicedBytes appended,
    // once the audio before has gone to it; see
    // SpeechTranscriber::beginUtterance.
    void beginUtterance(qsizetype voicedBytes);
    // Has the provider finalize the utterance appended so far, once that
    // audio has gone to it; see SpeechTranscriber::endUtterance.
    void endUtterance();
    // Where in the input the current attempt's audio starts.
    qint64 attemptStartMs() const;

    // Stops sending until resume().
    void hold();
    // Stops sending and cancels the current attempt, whose partial is
    // committed as nothing will finish it. Audio it was sent before it
    // connected is sent again.
    void dropAttempt();
    // Opens the next attempt on the provider and sends it what waits.
    void resume();
    // The stream's way through a failure of the current attempt, after the
    // owner's Recovery.
    void handleFailure(const SpeechFailure &failure);
    // Stops everything running and lets go of the audio.
    void release();

signals:
    // A provider takes the input from the start of the audio kept; the text
    // so far is gone.
    void startedOver();
    void progress(qreal fractionOfAudioSent);
    // The stream could not take the oldest audio, this much of it, which
    // was dropped.
    void audioLost(qint64 durationMs);
    // The provider answered all of the input.
    void finished();
    // The input could not be transcribed, or only in part, for message.
    void failed(const QString &message);

private:
    void beginStreaming();
    void startNextAttempt();
    void sendNextChunk();
    void handleCompleted();
    void markUtterancesOnceSent();
    void rewindTo(qsizetype position);
    void keepAudioBounded();
    void forgetAudioBefore(qsizetype position);

    ProviderRegistry *m_providers;
    SpeechAttempts *m_attempts;
    TranscriptState *m_transcript;
    SpeechAttemptOptions m_options;
    Recovery m_recovery;
    std::function<void()> m_rollover;
    qsizetype m_mostKeptBytes = 0;
    AppSettings m_settings;
    QTimer m_sendTimer;
    QByteArray m_pcm;
    // Audio dropped from the front of m_pcm once a connected stream took it,
    // or once too much waited, so a long input does not keep all it heard.
    qsizetype m_pcmDropped = 0;
    // Bytes sent, counted from the start of the input.
    qsizetype m_sent = 0;
    // m_sent when the current attempt began.
    qsizetype m_attemptStart = 0;
    // Where an utterance begins or ends in the input.
    struct UtteranceMark {
        qsizetype position;
        bool begins;
    };
    // Each beginUtterance() and endUtterance(), in order, kept until that
    // audio is dropped, so a stream that takes the audio again gets them
    // again.
    QList<UtteranceMark> m_utteranceMarks;
    // The first of m_utteranceMarks the current stream has not been sent.
    qsizetype m_nextUtteranceMark = 0;
    bool m_inputEnded = false;
    // All the input was sent and the provider told.
    bool m_inputFinished = false;
    // An attempt is open for the audio; false while a provider prepares or
    // the stream is held.
    bool m_streaming = false;
    // Why each speech provider the stream passed could not help.
    QList<ProviderAttemptIssue> m_issues;
};

} // namespace speecher
