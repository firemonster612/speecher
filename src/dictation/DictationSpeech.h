#pragma once

#include "core/AppSettings.h"
#include "core/ProviderAttemptIssue.h"
#include "core/ProviderReadiness.h"
#include "dictation/BetweenAttempts.h"
#include "providers/SpeechAttempts.h"

#include <QObject>
#include <QPointer>

#include <optional>

namespace speecher {

class ProviderRegistry;
class TranscriptRefiner;
class TranscriptState;

// A Dictation Session's speech, from its first speech provider until every
// word heard is in the Raw Transcript: the attempts it opens on the provider,
// through pauses, reconnects and rollovers, the audio held while no attempt
// can take it, and the walk on to the next speech provider when one fails.
// The session keeps the microphone and the states the person sees. Each
// change goes in one order: a command sets the speech's phase, with no word
// to a provider; the session announces its state, so a handler that reacts
// at once finds the speech there too; then proceed() does what the phase asks
// of the provider, whose callbacks find the session settled. The speech tells
// the session when the microphone may start, when the speech has ended and
// when a failure ends it.
class DictationSpeech : public QObject {
    Q_OBJECT

public:
    DictationSpeech(ProviderRegistry *providers, TranscriptState *transcript, QObject *parent = nullptr);

    // A new Dictation Session: nothing the last one's attempt still sends
    // counts, and audio waits for a provider.
    void begin();
    // Takes speech's provider and starts the chain over at it; false when the
    // registry does not offer it.
    bool selectProviders(const SpeechSettings &speech);
    // Walks the chain on from the provider it is on; the first that prepares
    // opens the first attempt and readyToListen() follows. refiner's sign-in
    // renews alongside. settings are the session's, which the speech runs
    // with from here on.
    void start(const AppSettings &settings, TranscriptRefiner *refiner);
    // The microphone has started for the first attempt; nothing is owed to
    // the provider.
    void listen();
    // Audio from the microphone, which the session took for the speech.
    void hear(const QByteArray &pcm);
    // The person paused, the microphone stopped: the open attempt is to
    // finish the words already spoken, and its end is no rollover.
    void pause();
    // The person resumed: the next attempt is to open, unless the paused one
    // still finishes or a provider prepares.
    void resume();
    // The person stopped. While listening, the open attempt takes the
    // post-roll until proceed(). Nothing once stopping already.
    void stop();
    // Once the session has announced the state the last command stands for,
    // and after a stop while listening its microphone has stopped for good:
    // a paused attempt is told the input ended, a resume's attempt opens, and
    // a stop tells the last attempt the input ended, lets go of one that
    // heard nothing since a resume, or gives audio held meanwhile an attempt
    // of its own; ended() follows once the words are in. Its callbacks may
    // move the session on.
    void proceed();
    // Nothing running reports back, and nothing held is kept.
    void cancel();

    // What the system says about the internet; providers that need it are
    // passed over while offline.
    void setReachability(Reachability reachability);
    // The speech providers selected, primary first.
    const QStringList &providerIds() const;
    // Whether the chain has fallbacks and issues says why any provider it
    // tried could not help.
    bool triedFallbacks(const QList<ProviderAttemptIssue> &issues) const;
    // Why no speech provider could take the audio: once triedFallbacks(),
    // each provider tried and why; otherwise providerMessage.
    QString failureText(const QString &providerMessage, const QList<ProviderAttemptIssue> &issues) const;

signals:
    // The first provider is ready and its attempt open: the session starts
    // the microphone, then listen(). A provider ready while it starts only
    // opens its attempt.
    void readyToListen();
    // Whether providerId's sign-in held: it prepared and takes the dictation
    // from here, or the service turned the sign-in down.
    void signInObserved(const QString &providerId, bool signedIn);
    // An attempt opened on providerId.
    void ran(const QString &providerId);
    // A provider the speech passed over, and why.
    void passedOver(const QString &providerId, speecher::ProviderAttemptIssue::Stage stage,
                    const speecher::ProviderFailure &failure);
    // A provider's sign-in renews before it can take the dictation.
    void renewingSignIn();
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
    // Where the speech is in the session, as its own commands set it.
    enum class Phase {
        // No session, or its speech has ended.
        Idle,
        // Before the session listens: the first provider prepares, its
        // attempt opens and the microphone starts.
        Starting,
        Listening,
        Paused,
        Stopping,
    };

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
    // Opens the next attempt, after a pause, a rollover or a reconnect, and
    // sends it the audio held for it.
    void resumeAttempt();
    // Sends the open attempt the audio that waited for one, in order.
    void sendHeldAudio();
    void endedWhileStopping();
    // The speech ends on what the attempts heard: audio still held gets an
    // attempt of its own first, then ended().
    void end();
    void endInput();
    void finishStop();
    void cancelRetiredAttempt();
    bool offline() const;

    SpeechAttempts *m_attempts;
    Phase m_phase = Phase::Idle;
    // What the speech runs with, from start() until it is cancelled.
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
    // While stopping: the microphone has stopped, and nothing more is heard.
    bool m_microphoneStopped = false;
    // The stop came while paused, or before a resume's attempt opened: no
    // attempt is open to be told the input ended.
    bool m_noAttemptToFinish = false;
    // Owed to the provider at proceed(): pause() left the open attempt to be
    // told the input ended, resume() the next attempt to open.
    bool m_finishOwed = false;
    bool m_openOwed = false;
    // The current attempt was told its input ended: no audio may follow.
    bool m_inputEnded = false;
    // The attempt a resume opened with nothing held for it, until it hears
    // audio; 0 for none.
    quint64 m_emptyResumedAttempt = 0;
    // That attempt, let go by a stop; its provider is told at proceed().
    quint64 m_retiredAttempt = 0;
};

} // namespace speecher
