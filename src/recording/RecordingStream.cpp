#include "recording/RecordingStream.h"

#include "core/TranscriptState.h"
#include "core/settings/SpokenLanguages.h"
#include "providers/ProviderRegistry.h"

#include <QDebug>

#include <algorithm>
#include <utility>

namespace speecher {
namespace {

using Stage = ProviderAttemptIssue::Stage;

constexpr int kBytesPerSecond = 16000 * 2;
// 100 ms of 16 kHz mono s16, the chunk size live capture sends.
constexpr qsizetype kChunkBytes = kBytesPerSecond / 10;
// How fast audio that waited for the stream catches up: one chunk every
// 12 ms, as a file is sent.
constexpr int kSendIntervalMs = 12;
// The pauses before each reconnect of a dropped stream; the last repeats for
// as long as the input runs.
constexpr int kReconnectDelaysMs[] = {1000, 2000, 5000, 10000, 30000};
// The most audio kept while the stream is down: ten minutes.
constexpr qsizetype kMostKeptBytes = qsizetype(10) * 60 * kBytesPerSecond;

// How many of marks, in order of position, fall before position.
template <typename Marks>
qsizetype marksBefore(const Marks &marks, qsizetype position)
{
    return std::ranges::lower_bound(marks, position, {}, [](const auto &mark) { return mark.position; })
        - marks.begin();
}

} // namespace

RecordingStream::RecordingStream(ProviderRegistry *providers, QObject *parent)
    : QObject(parent)
    , m_providers(providers)
    , m_speechChain(new SpeechChain(
          providers, [this](const QString &providerId) { return acquireTranscriber(providerId); }, this))
    , m_transcript(new TranscriptState(this))
{
    // The provider starts once the worker reports, never inside start().
    m_speechChain->setOptions({.streamedFinalsOnly = true, .readyLater = true});
    m_sendTimer.setInterval(kSendIntervalMs);
    m_sendTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_sendTimer, &QTimer::timeout, this, &RecordingStream::sendNextChunk);
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &RecordingStream::reconnect);
    connect(m_speechChain, &SpeechChain::passedOver, this,
            [this](const QString &providerId, Stage stage, const ProviderFailure &failure) {
                m_speechIssues.append({ProviderRole::Speech, providerId, stage, failure.kind, failure.message});
            });
    connect(m_speechChain, &SpeechChain::secretsResolved, this,
            [this](const AppSettings &settings) { m_settings = settings; });
    connect(m_speechChain, &SpeechChain::ready, this, &RecordingStream::beginStreaming);
    connect(m_speechChain, &SpeechChain::exhausted, this,
            [this] { fail(speechFailureText(m_speechIssues.value(0).message)); });
    connect(m_speechChain, &SpeechChain::signInRenewed, this, &RecordingStream::handleSignInRenewed);
}

RecordingStream::~RecordingStream()
{
    release();
}

bool RecordingStream::isRunning() const
{
    return m_running;
}

// A running stream always has its speech provider.
int RecordingStream::utteranceAnswerTimeoutMs() const
{
    return m_transcriber->utteranceAnswerTimeoutMs();
}

// The input fills m_pcm from before the providers prepare, and the send timer
// keeps up with it once one is streaming.
void RecordingStream::start(AudioInput *input, const AppSettings &settings)
{
    m_settings = settings;
    m_settings.speech.codexFinalRetranscribe = false;
    m_settings.speech.cutIntoUtterances = true;
    m_running = true;
    m_error.clear();
    m_transcript->clear();
    m_sent = 0;
    m_utteranceMarks.clear();
    m_nextUtteranceMark = 0;
    m_inputFinished = false;
    m_speechIssues.clear();
    m_input = input;
    connect(m_input, &AudioInput::audioChunk, this, [this](const QByteArray &pcm) {
        if (m_inputState == Input::Off) {
            return;
        }
        m_pcm += pcm;
        keepAudioBounded();
        if (m_streaming && !m_sendTimer.isActive()) {
            m_sendTimer.start();
        }
    });
    // An input that drops after it heard something still has that sent.
    connect(m_input, &AudioInput::failed, this, [this](const QString &message) {
        if (m_pcmDropped == 0 && m_pcm.isEmpty()) {
            fail(message);
            return;
        }
        m_error = message;
        finishListening();
    });
    m_inputState = Input::Listening;
    QString error;
    if (!m_input->start(&error)) {
        m_inputState = Input::Off;
        fail(error);
        return;
    }
    m_speechChain->begin(m_settings.speech.providerId, m_settings.speech.fallbackProviderIds);
    // The recording's own provider fails the stream.
    if (const QString problem =
            spokenLanguageProblem(currentSpeechSettings(), m_providers->speechProviderLabel(m_speechChain->currentId()));
        !problem.isEmpty()) {
        fail(problem);
        return;
    }
    m_speechChain->prepare(m_settings);
}

// AudioInput::stop() may spin an event loop for the post-roll; the audio it
// delivers meanwhile still counts, so the input only ends once it returns.
void RecordingStream::finishListening()
{
    if (m_inputState != Input::Listening) {
        return;
    }
    m_inputState = Input::Stopping;
    m_input->stop();
    m_inputState = Input::Off;
    // A stream waiting to reconnect tries once more, now, for the audio that
    // waits.
    if (m_reconnectTimer.isActive()) {
        m_reconnectTimer.stop();
        reconnect();
    } else if (m_streaming && !m_sendTimer.isActive()) {
        m_sendTimer.start();
    }
}

// A begin never goes before the mark ahead of it, as the lead-in of speech
// that runs on past an end belongs to the utterance that ended. Audio the
// input delivers as it stops can begin one too.
void RecordingStream::beginUtterance(qsizetype voicedBytes)
{
    if (m_inputState == Input::Off) {
        return;
    }
    qsizetype position = m_pcmDropped + m_pcm.size() - voicedBytes;
    if (!m_utteranceMarks.isEmpty()) {
        position = std::max(position, m_utteranceMarks.last().position);
    }
    m_utteranceMarks.append({position, true});
    markUtterancesOnceSent();
}

void RecordingStream::endUtterance()
{
    if (!m_running || m_inputFinished) {
        return;
    }
    m_utteranceMarks.append({m_pcmDropped + m_pcm.size(), false});
    markUtterancesOnceSent();
}

void RecordingStream::cancel()
{
    if (!m_running) {
        return;
    }
    release();
    m_running = false;
    emit finished({});
}

// A fresh transcriber for each provider the stream tries, never live
// dictation's.
SpeechTranscriber *RecordingStream::acquireTranscriber(const QString &providerId)
{
    releaseTranscriber();
    m_transcriber = m_providers->createSpeechProvider(providerId, this);
    if (m_transcriber) {
        connectTranscriber();
    }
    return m_transcriber;
}

// Why no speech provider could take the stream: with fallbacks, each one
// tried and why; otherwise the provider's own reason.
QString RecordingStream::speechFailureText(const QString &providerMessage) const
{
    if (m_speechChain->providerIds().size() == 1) {
        return providerMessage;
    }
    return noSpeechServiceText(m_speechIssues, [this](ProviderRole, const QString &id) {
        return m_providers->speechProviderLabel(id);
    });
}

void RecordingStream::connectTranscriber()
{
    connect(m_transcriber, &SpeechTranscriber::attemptConnected, this, [this](quint64 attemptId) {
        if (attemptId == m_attemptId) {
            markAttemptConnected();
        }
    });
    connect(m_transcriber, &SpeechTranscriber::partialTranscript, this,
            [this](quint64 attemptId, const QString &text) {
                if (attemptId == m_attemptId) {
                    markAttemptConnected();
                    m_transcript->setPartial(text);
                }
            });
    connect(m_transcriber, &SpeechTranscriber::finalTranscript, this,
            [this](quint64 attemptId, const QString &text) {
                if (attemptId == m_attemptId) {
                    markAttemptConnected();
                    commitFinal(text);
                }
            });
    connect(m_transcriber, &SpeechTranscriber::attemptTranscript, this,
            [this](quint64 attemptId, const QString &text) {
                if (attemptId == m_attemptId) {
                    m_transcript->replaceAttemptFinals(text);
                }
            });
    connect(m_transcriber, &SpeechTranscriber::utteranceFailed, this, [this](const SpeechFailure &failure) {
        if (failure.attemptId == m_attemptId) {
            emit utteranceFailed(failure.message);
        }
    });
    connect(m_transcriber, &SpeechTranscriber::attemptCompleted, this, &RecordingStream::handleAttemptCompleted);
    connect(m_transcriber, &SpeechTranscriber::failed, this, &RecordingStream::handleSpeechFailure);
}

// The provider takes the input from the oldest audio kept.
void RecordingStream::beginStreaming()
{
    m_transcript->clear();
    rewindTo(m_pcmDropped);
    m_attemptSentFrom = m_sent;
    m_attemptConnected = false;
    m_providerConnected = false;
    m_reconnectDelayIndex = 0;
    m_inputFinished = false;
    m_attempts.refill();
    m_attempts.attemptStarted();
    const quint64 attemptId = ++m_attemptId;
    m_transcriber->startAttempt(attemptId, currentSpeechSettings());
    // A provider failing inside startAttempt() has made way for the next, or
    // failed the stream: nothing may be sent for this attempt.
    if (attemptId != m_attemptId) {
        return;
    }
    m_streaming = true;
    m_sendTimer.start();
}

void RecordingStream::sendNextChunk()
{
    markUtterancesOnceSent();
    if (m_sent >= m_pcmDropped + m_pcm.size()) {
        m_sendTimer.stop();
        // The input's next chunk starts the timer again.
        if (m_inputState != Input::Off) {
            return;
        }
        m_inputFinished = true;
        m_transcriber->finishInput(m_attemptId);
        return;
    }
    // A send stops at the next utterance mark, which follows it at once.
    qsizetype chunkBytes = kChunkBytes;
    if (m_nextUtteranceMark < m_utteranceMarks.size()) {
        chunkBytes = std::min(chunkBytes, m_utteranceMarks.at(m_nextUtteranceMark).position - m_sent);
    }
    const QByteArray chunk = m_pcm.mid(m_sent - m_pcmDropped, chunkBytes);
    m_sent += chunk.size();
    const quint64 attemptId = m_attemptId;
    m_transcriber->sendAudio(attemptId, chunk);
    // A provider can fail the stream from inside sendAudio().
    if (attemptId != m_attemptId) {
        return;
    }
    markUtterancesOnceSent();
    // Once a stream has connected, no other provider takes the input from its
    // start (see handleSpeechFailure) and its next stream picks up at the next
    // unsent chunk, so what it was sent is never needed again.
    if (m_attemptConnected) {
        forgetAudioBefore(m_sent);
    }
}

// Audio a provider is still preparing for, or the send timer has yet to
// reach, waits; each mark goes after the audio heard before it.
void RecordingStream::markUtterancesOnceSent()
{
    while (m_streaming && m_nextUtteranceMark < m_utteranceMarks.size()
           && m_sent >= m_utteranceMarks.at(m_nextUtteranceMark).position) {
        if (m_utteranceMarks.at(m_nextUtteranceMark++).begins) {
            m_transcriber->beginUtterance(m_attemptId);
        } else {
            m_transcriber->endUtterance(m_attemptId);
        }
    }
}

// The current stream takes the input again from position, with the
// utterance marks from there on.
void RecordingStream::rewindTo(qsizetype position)
{
    m_sent = position;
    m_nextUtteranceMark = marksBefore(m_utteranceMarks, position);
}

// While the stream is down the input's audio waits for it, up to a limit;
// past it, the oldest goes.
void RecordingStream::keepAudioBounded()
{
    const qsizetype excess = m_pcm.size() - kMostKeptBytes;
    if (excess <= 0) {
        return;
    }
    forgetAudioBefore(m_pcmDropped + excess);
    emit audioLost(excess * 1000 / kBytesPerSecond);
}

// Audio before position is never sent again, nor the utterance marks within
// it.
void RecordingStream::forgetAudioBefore(qsizetype position)
{
    m_pcm.remove(0, position - m_pcmDropped);
    m_pcmDropped = position;
    m_sent = std::max(m_sent, position);
    m_attemptSentFrom = std::max(m_attemptSentFrom, position);
    const qsizetype passed = marksBefore(m_utteranceMarks, position);
    m_utteranceMarks.remove(0, passed);
    m_nextUtteranceMark = std::max<qsizetype>(0, m_nextUtteranceMark - passed);
}

void RecordingStream::markAttemptConnected()
{
    if (m_attemptConnected) {
        return;
    }
    m_attemptConnected = true;
    m_providerConnected = true;
    m_signInRefused = false;
    emit connected();
}

// A fresh stream on the same transcriber while the audio keeps flowing.
//
// Known limitation: audio the old stream received but had not transcribed yet
// is lost, leaving a silent gap in the transcript. The new stream picks up at
// the next unsent chunk because no provider reports how far into the audio its
// transcript reaches, so there is no position to rewind to; resending a guessed
// window would duplicate words instead.
void RecordingStream::startNextAttempt()
{
    commitPartial();
    const SpeechSettings speech = currentSpeechSettings();
    m_transcript->startAttempt(speech.language);
    m_attemptSentFrom = m_sent;
    m_attemptConnected = false;
    m_attempts.attemptStarted();
    m_transcriber->startAttempt(++m_attemptId, speech);
}

// A dropped stream waits a pause, growing while it keeps failing, before it
// reconnects; the input's audio waits with it, and a stream that never
// connected has its audio sent again. The partial it left is written now, as
// the provider will never finish it, and the stream is cancelled, as a final
// it still sent would repeat that partial.
void RecordingStream::waitToReconnect(const QString &reason)
{
    m_sendTimer.stop();
    m_streaming = false;
    commitPartial();
    m_transcriber->cancelAttempt(m_attemptId++);
    if (!m_attemptConnected) {
        rewindTo(m_attemptSentFrom);
    }
    if (m_attemptConnected && m_attempts.attemptWasStable()) {
        m_reconnectDelayIndex = 0;
    }
    const int delayMs = kReconnectDelaysMs[m_reconnectDelayIndex];
    m_reconnectDelayIndex = std::min<int>(m_reconnectDelayIndex + 1, std::size(kReconnectDelaysMs) - 1);
    qInfo().noquote() << QStringLiteral("recording stream dropped, reconnecting in %1 ms: %2").arg(delayMs).arg(reason);
    emit reconnecting(reason);
    m_reconnectTimer.start(delayMs);
}

// The next stream, after a drop or a rollover.
void RecordingStream::reconnect()
{
    m_sendTimer.stop();
    m_streaming = false;
    if (renewSignIn()) {
        return;
    }
    resumeStreaming();
}

// A recording outlasts its sign-in, so the next stream waits for it to renew
// when the provider says it is due, or when the service turned it down.
// Returns whether it is renewing.
bool RecordingStream::renewSignIn()
{
    const SpeechSettings speech = currentSpeechSettings();
    if (!m_signInRefused && !m_transcriber->requiresRefresh(speech, m_settings.accounts)) {
        return false;
    }
    qInfo() << "recording renews its speech sign-in";
    m_speechChain->renewSignIn(m_transcriber, speech, m_settings.accounts);
    return true;
}

// A renewal that failed fails the stream it was for: one that could not reach
// the service reconnects later, and one turned down stops it.
void RecordingStream::handleSignInRenewed(const std::optional<ProviderFailure> &failure)
{
    if (!failure) {
        resumeStreaming();
        return;
    }
    handleSpeechFailure(
        {m_attemptId, failure->message, isTransientFailure(failure->kind), SpeechPhase::Connect, failure->kind});
}

void RecordingStream::resumeStreaming()
{
    m_streaming = true;
    startNextAttempt();
    // The attempt can fail inside startAttempt() and wait again.
    if (m_streaming && !m_sendTimer.isActive()) {
        m_sendTimer.start();
    }
}

SpeechSettings RecordingStream::currentSpeechSettings() const
{
    SpeechSettings speech = m_settings.speech;
    speech.providerId = m_speechChain->currentId();
    return speech;
}

void RecordingStream::handleAttemptCompleted(quint64 attemptId)
{
    if (attemptId != m_attemptId) {
        return;
    }
    // The finals were the transcript as they came, and a stream that heard
    // nothing still ended well.
    if (m_inputFinished) {
        finish();
        return;
    }
    // The provider ended a stream before the input did (a clean server close).
    if (!m_attempts.attemptWasStable()) {
        handleSpeechFailure(SpeechAttemptPolicy::endedTooSoon(attemptId));
        return;
    }
    m_attempts.refill();
    qInfo() << "recording stream ended by the provider; rolling over";
    reconnect();
}

void RecordingStream::handleSpeechFailure(const SpeechFailure &failure)
{
    if (failure.attemptId != m_attemptId) {
        return;
    }
    if (m_providerConnected && m_inputState == Input::Listening) {
        // A sign-in the service turns down may only have expired: the stream
        // reconnects once with it renewed before that stops it. A provider
        // with nothing to renew would be turned down again.
        const bool firstRefusal = failure.kind == ProviderFailureKind::Authentication
            && m_transcriber->renewsSignIn() && !std::exchange(m_signInRefused, true);
        if (failure.retryable || firstRefusal) {
            waitToReconnect(failure.message);
            return;
        }
    }
    // Nothing reached a service yet: no audio went out, or the first attempt
    // never connected and only buffered what it was given, and no text came
    // back. Then the next provider may take the input instead, from the
    // oldest audio kept; after, never: what was sent is not sent again.
    const bool nothingReachedAService = m_attemptSentFrom == 0 && !m_providerConnected
        && (m_sent == 0 || failure.phase == SpeechPhase::Connect);
    const ProviderAttemptIssue issue{ProviderRole::Speech, m_speechChain->currentId(),
                                     nothingReachedAService ? Stage::Connect : Stage::Interrupted, failure.kind,
                                     failure.message};
    if (nothingReachedAService && m_speechChain->fallbackRemains(failure.kind)) {
        m_sendTimer.stop();
        m_speechIssues.append(issue);
        releaseTranscriber();
        m_speechChain->prepareNext(m_settings);
        return;
    }
    m_attempts.refillIfAttemptWasStable();
    const bool reconnectable = !m_inputFinished && failure.retryable && failure.phase == SpeechPhase::Streaming;
    if (reconnectable && m_attempts.takeReconnect()) {
        qInfo().noquote() << "recording stream dropped, reconnecting: " + failure.message;
        startNextAttempt();
        return;
    }
    m_speechIssues.append(issue);
    fail(m_transcript->isEmpty() ? speechFailureText(failure.message) : failure.message);
}

void RecordingStream::fail(const QString &message)
{
    qWarning().noquote() << "recording stream failed: " + message;
    // The provider will never finish the partial now.
    commitPartial();
    m_error = message;
    finish();
}

void RecordingStream::finish()
{
    release();
    m_running = false;
    emit finished(m_error);
}

// The id moves on first, so nothing the provider sends while it stops
// counts.
void RecordingStream::releaseTranscriber()
{
    if (!m_transcriber) {
        return;
    }
    disconnect(m_transcriber, nullptr, this, nullptr);
    m_streaming = false;
    m_transcriber->cancelAttempt(m_attemptId++);
    m_transcriber->deleteLater();
    m_transcriber = nullptr;
}

void RecordingStream::release()
{
    m_sendTimer.stop();
    m_reconnectTimer.stop();
    m_speechChain->cancel();
    m_signInRefused = false;
    releaseTranscriber();
    if (m_input) {
        disconnect(m_input, nullptr, this, nullptr);
        if (std::exchange(m_inputState, Input::Off) == Input::Listening) {
            m_input->stop();
        }
    }
    m_pcm.clear();
    m_pcmDropped = 0;
}

void RecordingStream::commitFinal(const QString &text)
{
    m_transcript->commitFinal(text);
    emit textFinalized(text);
}

void RecordingStream::commitPartial()
{
    const QString partial = m_transcript->partial();
    if (!partial.isEmpty()) {
        commitFinal(partial);
    }
}

} // namespace speecher
