#include "dictation/DictationSpeech.h"

#include "core/settings/SpokenLanguages.h"

#include <QDebug>

#include <utility>

namespace speecher {
namespace {
// The most audio held between attempts: the connect budget's worth, 10
// seconds of 16 kHz mono 16-bit PCM.
constexpr qsizetype kMaxPendingAudioBytes = 10 * 16000 * 2;
}

DictationSpeech::DictationSpeech(ProviderRegistry *providers,
                                 TranscriptState *transcript,
                                 std::function<DictationState()> sessionState,
                                 std::function<bool()> microphoneStopping,
                                 QObject *parent)
    : QObject(parent)
    , m_attempts(new SpeechAttempts(providers, SpeechAttempts::Transcribers::Shared, transcript, this))
    , m_sessionState(std::move(sessionState))
    , m_microphoneStopping(std::move(microphoneStopping))
{
    // A late word never repaints the preview once refinement has it.
    m_attempts->setAcceptsText([this] {
        const DictationState state = m_sessionState();
        return state == DictationState::Starting || state == DictationState::Listening
            || state == DictationState::Paused || state == DictationState::Stopping;
    });
    connect(m_attempts, &SpeechAttempts::completed, this, &DictationSpeech::handleCompleted);
    connect(m_attempts, &SpeechAttempts::failed, this, &DictationSpeech::handleFailure);
    SpeechChain *chain = m_attempts->chain();
    connect(chain, &SpeechChain::passedOver, this, &DictationSpeech::passedOver);
    connect(chain, &SpeechChain::secretsResolved, this, [this](const AppSettings &settings) {
        if (m_between.awaitingProvider && m_settings) {
            m_settings = settings;
            emit secretsResolved(settings);
        }
    });
    connect(chain, &SpeechChain::ready, this, &DictationSpeech::takeReadyProvider);
    connect(chain, &SpeechChain::exhausted, this, &DictationSpeech::exhaust);
}

SpeechAttempts *DictationSpeech::attempts() const
{
    return m_attempts;
}

void DictationSpeech::begin()
{
    // The last session's attempt has ended; nothing it still sends counts.
    m_attempts->forgetAttempt();
    m_settings.reset();
    m_between = {};
    m_between.awaitingProvider = true;
    m_microphoneStarting = false;
}

bool DictationSpeech::selectProviders(const SpeechSettings &speech)
{
    if (!m_attempts->acquire(speech.providerId)) {
        return false;
    }
    m_attempts->chain()->begin(speech.providerId, speech.fallbackProviderIds);
    return true;
}

void DictationSpeech::prepare(const AppSettings &settings, TranscriptRefiner *refiner)
{
    m_settings = settings;
    m_refiner = refiner;
    m_attempts->chain()->prepare(settings, refinerToRenew());
}

// While starting, the refiner's sign-in renews alongside the speech
// provider's, so refinement need not wait for it later.
TranscriptRefiner *DictationSpeech::refinerToRenew() const
{
    return m_sessionState() == DictationState::Starting ? m_refiner.data() : nullptr;
}

void DictationSpeech::hear(const QByteArray &pcm)
{
    if (m_between.holdsAudio()) {
        hold(pcm);
        return;
    }
    // None once no provider is left: the speech is ending.
    if (m_attempts->transcriber()) {
        m_attempts->sendAudio(pcm);
    }
}

// The prepared provider takes over where the session is: it opens the
// first attempt while starting, and later the attempt for the audio heard
// while it prepared.
void DictationSpeech::takeReadyProvider()
{
    if (!m_between.awaitingProvider || !m_settings) {
        qInfo() << "speech preparation result ignored";
        return;
    }
    emit providerReady(m_attempts->chain()->currentId());
    m_between.awaitingProvider = false;
    switch (m_sessionState()) {
    case DictationState::Starting:
        if (m_microphoneStarting) {
            resumeAttempt();
        } else {
            openFirstAttempt();
        }
        break;
    case DictationState::Listening:
        resumeAttempt();
        break;
    case DictationState::Paused:
        // What was heard before the pause goes to it now, as a pause would
        // send it; otherwise resume opens its first attempt.
        if (m_between.hasHeldAudio()) {
            resumeAttempt();
            if (!m_between.awaitingProvider) {
                m_between.finishingPausedAttempt = true;
                m_attempts->finishInput();
            }
        }
        break;
    case DictationState::Stopping:
        endedWhileStopping();
        break;
    case DictationState::Idle:
    case DictationState::Refining:
    case DictationState::Delivering:
    case DictationState::Error:
    case DictationState::Reviewing:
        break;
    }
}

// No provider is left to take the dictation.
void DictationSpeech::exhaust()
{
    m_between.awaitingProvider = false;
    m_between.dropHeld();
    emit exhausted(m_switchFailure);
}

// No more than the connect budget's worth of audio is held. A paused attempt
// still finishing, or the last one ending during the stop, is never cut
// short: past the budget, the audio is dropped from there on, and the
// session stops listening, so the attempt finishes and what is held goes to
// a final one. A provider still preparing past it is passed over and the
// speech ends with what was heard.
void DictationSpeech::hold(const QByteArray &pcm)
{
    // Whichever provider takes the held audio, nothing after the budget joins it.
    if (m_between.heldPastTheBudget) {
        return;
    }
    if (!m_between.awaitingProvider) {
        if (m_between.heldBytes() + pcm.size() <= kMaxPendingAudioBytes) {
            m_between.hold(pcm);
            return;
        }
        m_between.heldPastTheBudget = true;
        qWarning() << "speech audio held past the connect budget while an attempt finishes; dropping";
        emit heldPastTheBudget();
        return;
    }
    m_between.hold(pcm);
    if (m_between.heldBytes() <= kMaxPendingAudioBytes) {
        return;
    }
    qWarning() << "speech provider still preparing past the connect budget";
    m_attempts->chain()->cancel();
    emit passedOver(m_attempts->chain()->currentId(), Stage::Prepare,
                    {ProviderFailureKind::Timeout, QStringLiteral("It took too long to start")});
    exhaust();
}

// Retires the current attempt's provider and walks on to the next.
void DictationSpeech::switchProvider(const SpeechFailure &failure)
{
    m_switchFailure = failure;
    retireAttempt();
    m_attempts->chain()->prepareNext(*m_settings, refinerToRenew());
}

// The current speech provider is done with this dictation, whether another
// takes over or none is left. One that never connected couldn't be reached;
// any other dropped.
void DictationSpeech::noteFailure(const SpeechFailure &failure)
{
    const bool connecting =
        failure.phase == SpeechPhase::Connect || m_sessionState() == DictationState::Starting;
    emit passedOver(m_attempts->chain()->currentId(), connecting ? Stage::Connect : Stage::Interrupted,
                    {failure.kind, failure.message, failure.httpStatus});
}

// Nothing the attempt's provider sends from here on counts, and audio waits
// in m_between for the next provider, never in the failed one's buffers.
void DictationSpeech::retireAttempt()
{
    m_between.awaitingProvider = true;
    // The retired attempt will never finalise its last utterance.
    m_attempts->commitPartial();
    m_attempts->release();
}

bool DictationSpeech::offline() const
{
    return m_settings
        && m_attempts->chain()->offlineFor(ProviderRole::Speech, m_attempts->chain()->currentId(), *m_settings);
}

bool DictationSpeech::pause()
{
    // Paused again before the last pause's attempt finished: it is still
    // finishing, and what was heard since waits in m_between. Between
    // providers no attempt is open: the next one's readiness sends the words.
    const bool alreadyFinishing = std::exchange(m_between.finishingPausedAttempt, !m_between.awaitingProvider);
    return !alreadyFinishing;
}

void DictationSpeech::finishPausedAttempt()
{
    if (!m_between.awaitingProvider) {
        m_attempts->finishInput();
    }
}

bool DictationSpeech::resume()
{
    qInfo() << "speech resumes pausedAttemptFinished=" << !m_between.finishingPausedAttempt;
    // Between providers the attempt opens once the next one is ready.
    const bool opensAttempt = !m_between.finishingPausedAttempt && !m_between.awaitingProvider;
    const bool openedEmptyAttempt = opensAttempt && !m_between.hasHeldAudio();
    if (opensAttempt) {
        resumeAttempt();
    }
    return openedEmptyAttempt;
}

void DictationSpeech::stop()
{
    // The last attempt ended during the stop and the post-roll since waits:
    // it gets an attempt of its own before the speech ends.
    if (m_between.endedDuringStop) {
        takeAttemptEndedDuringStop();
        return;
    }
    // A paused attempt still finishing was already told; its end, or the next
    // attempt's, ends the speech. Between providers, the next one's
    // readiness does.
    if (m_attempts->transcriber() && !m_between.finishingPausedAttempt && !m_between.awaitingProvider) {
        m_attempts->finishInput();
    }
}

void DictationSpeech::takeAttemptEndedDuringStop()
{
    if (std::exchange(m_between.endedDuringStop, false)) {
        end();
    }
}

void DictationSpeech::stopWhilePaused()
{
    if (!m_between.finishingPausedAttempt) {
        end();
    }
}

// Audio heard after a resume reaches an attempt of its own before the speech
// ends: open it, finish it, and end once it has finished.
void DictationSpeech::end()
{
    // Between providers, the next one's readiness comes back here.
    if (m_between.awaitingProvider) {
        return;
    }
    if (m_between.hasHeldAudio()) {
        resumeAttempt();
        if (!m_between.awaitingProvider) {
            m_attempts->finishInput();
        }
        return;
    }
    emit ended();
}

void DictationSpeech::cancelAttempt()
{
    m_attempts->cancel();
}

void DictationSpeech::cancel()
{
    // The next provider may be preparing.
    m_attempts->chain()->cancel();
    m_attempts->cancel();
    m_settings.reset();
    m_between = {};
}

void DictationSpeech::resumeAttempt()
{
    startNextAttempt();
    sendHeldAudio();
}

// A provider that fails on the way makes way for the next, which gets what is
// still waiting; what the failed one took is never sent again.
void DictationSpeech::sendHeldAudio()
{
    while (!m_between.awaitingProvider && m_between.hasHeldAudio()) {
        m_attempts->sendAudio(m_between.takeOldestHeld());
    }
}

// In Stopping, the last attempt before the speech ends ended. While the
// microphone is still stopping its post-roll keeps arriving, so the stop
// takes over once it returns.
void DictationSpeech::endedWhileStopping()
{
    if (m_microphoneStopping()) {
        m_between.endedDuringStop = true;
        return;
    }
    end();
}

// The first attempt, before the microphone starts. A provider can fail inside
// startAttempt(), or as the audio that waited goes to it: the speech has
// moved on to the next one, or ended, and the microphone must not start for
// this one.
void DictationSpeech::openFirstAttempt()
{
    const QString providerId = m_attempts->chain()->currentId();
    emit ran(providerId);
    if (!m_attempts->startAttempt(chainSpeechSettings(m_settings->speech, providerId), {})) {
        return;
    }
    const quint64 attemptId = m_attempts->currentId();
    sendHeldAudio();
    if (m_sessionState() != DictationState::Starting || attemptId != m_attempts->currentId()) {
        return;
    }
    m_microphoneStarting = true;
    emit readyToListen();
    m_microphoneStarting = false;
}

// Opens a fresh attempt on the same transcriber while the microphone keeps
// running; the transcriber buffers audio until the new stream is ready.
void DictationSpeech::startNextAttempt()
{
    const QString providerId = m_attempts->chain()->currentId();
    emit ran(providerId);
    m_attempts->startAttempt(chainSpeechSettings(m_settings->speech, providerId), {});
}

void DictationSpeech::rollOver()
{
    // The provider ended a healthy stream on its own (a clean server close)
    // while the person is still talking. That is routine: no warning, and
    // the healthy stream refills the error budget.
    if (!m_attempts->takeRollover()) {
        handleFailure(m_attempts->endedTooSoon());
        return;
    }
    qInfo() << "speech stream ended by the provider; rolling over to attempt" << m_attempts->currentId() + 1;
    startNextAttempt();
}

void DictationSpeech::handleCompleted()
{
    const bool pausedAttempt = std::exchange(m_between.finishingPausedAttempt, false);
    const DictationState state = m_sessionState();
    if (state == DictationState::Stopping) {
        endedWhileStopping();
    } else if (state == DictationState::Listening) {
        pausedAttempt ? resumeAttempt() : rollOver();
    }
    // While still paused the words are in; resume opens the next attempt.
}

void DictationSpeech::handleFailure(const SpeechFailure &failure)
{
    if (failure.kind == ProviderFailureKind::Authentication) {
        emit signInTurnedDown(m_attempts->chain()->currentId());
    }
    const bool droppedStream = failure.retryable && failure.phase == SpeechPhase::Streaming;
    // A paused attempt that fails to finish (a stream closed during a long
    // pause) has still ended: resume opens a fresh one.
    if (std::exchange(m_between.finishingPausedAttempt, false)) {
        qInfo().noquote() << "paused speech attempt ended without finishing reason=" + failure.message;
        // Its last words may not have arrived.
        emit wordsMayBeMissing(failure);
        // A provider that turned the attempt away, rather than a stream that
        // closed, makes way for the next before the next attempt: at resume,
        // or now for words heard since one. Offline, so does a closed stream.
        const bool attemptNeeded = m_sessionState() != DictationState::Stopping || m_between.hasHeldAudio();
        const bool isOffline = offline();
        if ((!droppedStream || isOffline) && attemptNeeded && m_attempts->chain()->fallbackRemains(failure.kind)) {
            noteFailure(failure);
            switchProvider(failure);
            return;
        }
        const DictationState state = m_sessionState();
        if (state == DictationState::Paused) {
            return;
        }
        // Offline with no provider left, the speech ends below.
        if (state == DictationState::Listening && !isOffline) {
            resumeAttempt();
            return;
        }
        if (state == DictationState::Stopping) {
            // Nothing is left for it to hear: its part of the dictation ends here.
            if (!attemptNeeded) {
                noteFailure(failure);
            }
            endedWhileStopping();
            return;
        }
    }
    const DictationState state = m_sessionState();
    if (state != DictationState::Starting && state != DictationState::Listening && state != DictationState::Stopping) {
        return;
    }
    // Offline, a reconnect could only fail again.
    if (state == DictationState::Listening && m_settings && !offline() && m_attempts->takeReconnect(failure)) {
        // A dropped stream mid-sentence is a transient connection loss, not the
        // end of the Dictation Session.
        emit wordsMayBeMissing(failure);
        qInfo().noquote() << "speech stream reconnecting attempt=" << m_attempts->currentId() + 1
                          << "reason=" + failure.message;
        startNextAttempt();
        return;
    }
    // Reconnects spent, or a failure they can't mend: the provider is done
    // with this dictation, and the next one takes over. Not once stopped,
    // when the last attempt already has all the audio there is and none may
    // be sent again.
    noteFailure(failure);
    if (m_sessionState() != DictationState::Stopping && m_attempts->chain()->fallbackRemains(failure.kind)) {
        switchProvider(failure);
        return;
    }
    emit failed(failure);
}

} // namespace speecher
