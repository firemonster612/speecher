#pragma once

#include "core/AppSettings.h"
#include "core/ProviderAttemptIssue.h"
#include "dictation/BetweenAttempts.h"
#include "dictation/DictationTypes.h"
#include "providers/SpeechAttempts.h"

#include <QObject>
#include <QPointer>

#include <functional>
#include <optional>

namespace speecher {

class ProviderRegistry;
class TranscriptRefiner;
class TranscriptState;

// A Dictation Session's speech, from its first speech provider until every
// word heard is in the Raw Transcript: the attempts it opens on the provider,
// through pauses, reconnects and rollovers, the audio held while no attempt
// can take it, and the walk on to the next speech provider when one fails.
// The session keeps the microphone and the states the person sees; the
// speech reads them as it goes and tells the session when the microphone may
// start, when the speech has ended and when a failure ends it.
class DictationSpeech : public QObject {
    Q_OBJECT

public:
    // sessionState is the session's state at the moment the speech asks;
    // microphoneStopping is true while the microphone stops, its post-roll
    // still arriving.
    DictationSpeech(ProviderRegistry *providers,
                    TranscriptState *transcript,
                    std::function<DictationState()> sessionState,
                    std::function<bool()> microphoneStopping,
                    QObject *parent = nullptr);

    // The speech providers and their attempts, for what the session reports
    // about them.
    SpeechAttempts *attempts() const;

    // A new Dictation Session: nothing the last one's attempt still sends
    // counts, and audio waits for a provider.
    void begin();
    // Takes speech's provider and starts the chain over at it; false when the
    // registry does not offer it.
    bool selectProviders(const SpeechSettings &speech);
    // Walks the chain on from the provider it is on; the first that prepares
    // takes the next attempt. While starting, refiner's sign-in renews
    // alongside. settings are the session's, which the speech runs with from
    // here on.
    void prepare(const AppSettings &settings, TranscriptRefiner *refiner);
    // Audio from the microphone, which the session took for the speech.
    void hear(const QByteArray &pcm);

    // Before the session shows Paused: the open attempt is to finish the words
    // already spoken, and its end is no rollover. True when it still has to
    // be told, with finishPausedAttempt() once the session is Paused.
    bool pause();
    void finishPausedAttempt();
    // Once the session listens again after a pause: opens the next attempt,
    // unless the paused one still finishes or a provider prepares. True when
    // it opened one with no audio held for it.
    bool resume();
    // Once the microphone has stopped for good, post-roll and all: the last
    // attempt is told the input ended. ended() follows once its words are in.
    void stop();
    // Once the microphone has stopped: an attempt that ended during the stop
    // has the post-roll held since go to an attempt of its own, as stop()
    // would.
    void takeAttemptEndedDuringStop();
    // A stop while paused: once the paused attempt has finished, the speech
    // ends.
    void stopWhilePaused();
    // The speech ends on what the attempts heard: audio still held gets an
    // attempt of its own first, then ended().
    void end();
    // Nothing the current attempt sends from here on counts.
    void cancelAttempt();
    // Nothing running reports back, and nothing held is kept.
    void cancel();

signals:
    // The first provider is ready and its attempt open: the session starts
    // the microphone. A provider ready while it starts only opens its attempt.
    void readyToListen();
    // A provider prepared and takes the dictation from here.
    void providerReady(const QString &providerId);
    // An attempt opened on providerId.
    void ran(const QString &providerId);
    // A provider the speech passed over, and why.
    void passedOver(const QString &providerId, speecher::ProviderAttemptIssue::Stage stage,
                    const speecher::ProviderFailure &failure);
    // The service turned providerId's sign-in down.
    void signInTurnedDown(const QString &providerId);
    // The keys a provider's preparation read, in the session's settings.
    void secretsResolved(const speecher::AppSettings &settings);
    // An attempt ended by failure, before its last words may have arrived.
    void wordsMayBeMissing(const speecher::SpeechFailure &failure);
    // Too much was said while an attempt finished; what follows is dropped
    // until an attempt takes what is held.
    void heldPastTheBudget();
    // Every word heard is in the transcript.
    void ended();
    // failure ends the speech; no provider takes over.
    void failed(const speecher::SpeechFailure &failure);
    // No provider is left to take the dictation, after switchFailure sent it
    // on, if one did.
    void exhausted(const speecher::SpeechFailure &switchFailure);

private:
    using Stage = ProviderAttemptIssue::Stage;

    void takeReadyProvider();
    void exhaust();
    void hold(const QByteArray &pcm);
    TranscriptRefiner *refinerToRenew() const;
    void handleFailure(const SpeechFailure &failure);
    void handleCompleted();
    void noteFailure(const SpeechFailure &failure);
    void switchProvider(const SpeechFailure &failure);
    void retireAttempt();
    void rollOver();
    void openFirstAttempt();
    void startNextAttempt();
    // Opens the attempt after a pause once the paused one has finished, and
    // sends it the audio heard in between.
    void resumeAttempt();
    // Sends the open attempt the audio that waited for one, in order.
    void sendHeldAudio();
    void endedWhileStopping();
    bool offline() const;

    SpeechAttempts *m_attempts;
    std::function<DictationState()> m_sessionState;
    std::function<bool()> m_microphoneStopping;
    // What the speech runs with, from prepare() until it is cancelled.
    std::optional<AppSettings> m_settings;
    // The refiner whose sign-in renews while starting; the registry owns it.
    QPointer<TranscriptRefiner> m_refiner;
    BetweenAttempts m_between;
    // The failure that sent the speech on to the next provider, which ends
    // it if none takes over.
    SpeechFailure m_switchFailure;
    // The session starts the microphone for the first attempt; it spins an
    // event loop, and a provider that becomes ready meanwhile opens only its
    // attempt.
    bool m_microphoneStarting = false;
};

} // namespace speecher
