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

// What became of one of a Dictation Session's speech providers, for its
// Provider History.
struct SpeechProviderNote {
    enum class Kind {
        // It prepared and takes the dictation from here.
        Ready,
        // An attempt opened on it.
        Ran,
        // The service turned its sign-in down.
        SignInTurnedDown,
        // The speech passed it over at stage, for failure.
        PassedOver,
    };
    Kind kind = Kind::Ran;
    QString providerId;
    ProviderAttemptIssue::Stage stage = ProviderAttemptIssue::Stage::Prepare;
    ProviderFailure failure;
};

// A Dictation Session's speech, from its first speech provider until every
// word heard is in the Raw Transcript: the attempts it opens on the provider,
// through pauses, reconnects and rollovers, the audio held while no attempt
// can take it, and the walk on to the next speech provider when one fails.
// The session keeps the microphone and the states the person sees, and tells
// the speech when the person pauses, resumes and stops; the speech tells the
// session when the microphone may start, when the speech has ended and when a
// failure ends it.
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
    // Audio from the microphone, which the session took for the speech.
    void hear(const QByteArray &pcm);
    // Once the session is Paused, its microphone stopped: the open attempt
    // finishes the words already spoken, and its end is no rollover.
    void pause();
    // Once the session listens again after a pause: opens the next attempt,
    // unless the paused one still finishes or a provider prepares.
    void resume();
    // The person stopped. From a pause, the speech ends once the paused
    // attempt has finished. Otherwise the open attempt takes the post-roll
    // until microphoneStopped(). Nothing once stopping already.
    void stop();
    // Once the microphone has stopped for good after stop(), post-roll and
    // all: the last attempt is told the input ended, one that heard nothing
    // since a resume is let go, and post-roll held after an attempt ended
    // during the stop goes to an attempt of its own. ended() follows once
    // the words are in.
    void microphoneStopped();
    // Nothing running reports back, and nothing held is kept.
    void cancel();

    // What the system says about the internet; providers that need it are
    // passed over while offline.
    void setReachability(Reachability reachability);
    // Whether the system says it is offline and providerId needs the internet.
    bool offlineFor(ProviderRole role, const QString &providerId, const AppSettings &settings) const;
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
    // the microphone, and once this returns the speech listens, unless it
    // ended meanwhile. A provider ready while it starts only opens its
    // attempt.
    void readyToListen();
    // For the session's Provider History.
    void providerNoted(const speecher::SpeechProviderNote &note);
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
    void startNextAttempt();
    // Opens the attempt after a pause once the paused one has finished, and
    // sends it the audio heard in between.
    void resumeAttempt();
    // Sends the open attempt the audio that waited for one, in order.
    void sendHeldAudio();
    void endedWhileStopping();
    // The speech ends on what the attempts heard: audio still held gets an
    // attempt of its own first, then ended().
    void end();
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
    // The attempt a resume opened with nothing held for it, until it hears
    // audio; 0 for none.
    quint64 m_emptyResumedAttempt = 0;
};

} // namespace speecher
