#include "providers/SpeechStream.h"

#include "core/TranscriptState.h"
#include "core/settings/SpokenLanguages.h"
#include "providers/ProviderRegistry.h"
#include "providers/SpeechAudio.h"

#include <QDebug>

#include <algorithm>
#include <utility>

namespace speecher {
namespace {

using Stage = ProviderAttemptIssue::Stage;

// How many of marks, in order of position, fall before position.
template <typename Marks>
qsizetype marksBefore(const Marks &marks, qsizetype position)
{
    return std::ranges::lower_bound(marks, position, {}, [](const auto &mark) { return mark.position; })
        - marks.begin();
}

} // namespace

SpeechStream::SpeechStream(ProviderRegistry *providers, TranscriptState *transcript, SpeechAttemptOptions options,
                           QObject *parent)
    : QObject(parent)
    , m_providers(providers)
    , m_attempts(new SpeechAttempts(providers, SpeechAttempts::Transcribers::Fresh, transcript, this))
    , m_transcript(transcript)
    , m_options(options)
{
    m_sendTimer.setInterval(kSpeechCatchUpIntervalMs);
    m_sendTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_sendTimer, &QTimer::timeout, this, &SpeechStream::sendNextChunk);
    SpeechChain *chain = m_attempts->chain();
    connect(chain, &SpeechChain::passedOver, this,
            [this](const QString &providerId, Stage stage, const ProviderFailure &failure) {
                m_issues.append({ProviderRole::Speech, providerId, stage, failure.kind, failure.message});
            });
    connect(chain, &SpeechChain::secretsResolved, this, [this](const AppSettings &settings) { m_settings = settings; });
    connect(chain, &SpeechChain::ready, this, &SpeechStream::beginStreaming);
    connect(chain, &SpeechChain::exhausted, this,
            [this] { emit failed(m_attempts->failureText(m_issues.value(0).message, m_issues)); });
    connect(m_attempts, &SpeechAttempts::completed, this, &SpeechStream::handleCompleted);
    connect(m_attempts, &SpeechAttempts::failed, this, &SpeechStream::handleFailure);
}

SpeechAttempts *SpeechStream::attempts() const
{
    return m_attempts;
}

SpeechTranscriber *SpeechStream::transcriber() const
{
    return m_attempts->transcriber();
}

void SpeechStream::renewSignIn()
{
    m_attempts->chain()->renewSignIn(transcriber(), speechSettings(), m_settings.accounts);
}

void SpeechStream::setRecovery(Recovery recovery)
{
    m_recovery = std::move(recovery);
}

void SpeechStream::setRollover(std::function<void()> rollover)
{
    m_rollover = std::move(rollover);
}

void SpeechStream::setMostKeptBytes(qsizetype bytes)
{
    m_mostKeptBytes = bytes;
}

void SpeechStream::reset()
{
    m_transcript->clear();
    m_pcm.clear();
    m_pcmDropped = 0;
    m_sent = 0;
    m_attemptStart = 0;
    m_utteranceMarks.clear();
    m_nextUtteranceMark = 0;
    m_inputEnded = false;
    m_inputFinished = false;
    m_issues.clear();
}

void SpeechStream::prepare(const AppSettings &settings)
{
    m_settings = settings;
    SpeechChain *chain = m_attempts->chain();
    chain->begin(m_settings.speech.providerId, m_settings.speech.fallbackProviderIds);
    if (const QString problem =
            spokenLanguageProblem(speechSettings(), m_providers->speechProviderLabel(chain->currentId()));
        !problem.isEmpty()) {
        emit failed(problem);
        return;
    }
    chain->prepare(m_settings);
}

const AppSettings &SpeechStream::settings() const
{
    return m_settings;
}

SpeechSettings SpeechStream::speechSettings() const
{
    return chainSpeechSettings(m_settings.speech, m_attempts->chain()->currentId());
}

void SpeechStream::append(const QByteArray &pcm)
{
    m_pcm += pcm;
    keepAudioBounded();
    if (m_streaming && !m_sendTimer.isActive()) {
        m_sendTimer.start();
    }
}

bool SpeechStream::hasAudio() const
{
    return m_pcmDropped > 0 || !m_pcm.isEmpty();
}

void SpeechStream::endInput()
{
    m_inputEnded = true;
    if (m_streaming && !m_sendTimer.isActive()) {
        m_sendTimer.start();
    }
}

// A begin never goes before the mark ahead of it, as the lead-in of speech
// that runs on past an end belongs to the utterance that ended.
void SpeechStream::beginUtterance(qsizetype voicedBytes)
{
    qsizetype position = m_pcmDropped + m_pcm.size() - voicedBytes;
    if (!m_utteranceMarks.isEmpty()) {
        position = std::max(position, m_utteranceMarks.last().position);
    }
    m_utteranceMarks.append({position, true});
    markUtterancesOnceSent();
}

void SpeechStream::endUtterance()
{
    if (m_inputFinished) {
        return;
    }
    m_utteranceMarks.append({m_pcmDropped + m_pcm.size(), false});
    markUtterancesOnceSent();
}

qint64 SpeechStream::attemptStartMs() const
{
    return m_attemptStart * 1000 / kSpeechBytesPerSecond;
}

void SpeechStream::hold()
{
    m_sendTimer.stop();
    m_streaming = false;
}

void SpeechStream::dropAttempt()
{
    hold();
    m_attempts->commitPartial();
    m_attempts->cancel();
    if (!m_attempts->attemptConnected()) {
        rewindTo(std::max(m_attemptStart, m_pcmDropped));
    }
}

void SpeechStream::resume()
{
    m_streaming = true;
    startNextAttempt();
    // The attempt can fail inside startAttempt() and hold the stream again.
    if (m_streaming && !m_sendTimer.isActive()) {
        m_sendTimer.start();
    }
}

void SpeechStream::release()
{
    hold();
    m_attempts->chain()->cancel();
    m_attempts->release();
    m_pcm.clear();
    m_pcmDropped = 0;
}

// The provider takes the input from the oldest audio kept.
void SpeechStream::beginStreaming()
{
    m_transcript->clear();
    emit startedOver();
    rewindTo(m_pcmDropped);
    m_inputFinished = false;
    m_attemptStart = m_sent;
    // A provider failing inside startAttempt() has made way for the next, or
    // failed the stream: nothing may be sent for this attempt.
    if (!m_attempts->startAttempt(speechSettings(), m_options)) {
        return;
    }
    m_streaming = true;
    m_sendTimer.start();
}

// A fresh stream on the same transcriber while the audio keeps flowing.
//
// Known limitation: audio the old stream received but had not transcribed yet
// is lost, leaving a silent gap in the transcript. The new stream picks up at
// the next unsent chunk because no provider reports how far into the audio its
// transcript reaches, so there is no position to rewind to; resending a guessed
// window would duplicate words instead.
void SpeechStream::startNextAttempt()
{
    m_attemptStart = m_sent;
    m_attempts->startAttempt(speechSettings(), m_options);
}

void SpeechStream::sendNextChunk()
{
    markUtterancesOnceSent();
    if (m_sent >= m_pcmDropped + m_pcm.size()) {
        m_sendTimer.stop();
        // The input's next chunk starts the timer again.
        if (!m_inputEnded) {
            return;
        }
        m_inputFinished = true;
        m_attempts->finishInput();
        return;
    }
    // A send stops at the next utterance mark, which follows it at once.
    qsizetype chunkBytes = kSpeechChunkBytes;
    if (m_nextUtteranceMark < m_utteranceMarks.size()) {
        chunkBytes = std::min(chunkBytes, m_utteranceMarks.at(m_nextUtteranceMark).position - m_sent);
    }
    const QByteArray chunk = m_pcm.mid(m_sent - m_pcmDropped, chunkBytes);
    m_sent += chunk.size();
    // A provider can fail the stream from inside sendAudio().
    if (!m_attempts->sendAudio(chunk)) {
        return;
    }
    markUtterancesOnceSent();
    emit progress(qreal(m_sent) / qreal(m_pcmDropped + m_pcm.size()));
    // Once a stream has connected, no other provider takes the input from its
    // start (see handleFailure) and its next stream picks up at the next
    // unsent chunk, so what it was sent is never needed again.
    if (m_attempts->attemptConnected()) {
        forgetAudioBefore(m_sent);
    }
}

// Audio a provider is still preparing for, or the send timer has yet to
// reach, waits; each mark goes after the audio heard before it.
void SpeechStream::markUtterancesOnceSent()
{
    while (m_streaming && m_nextUtteranceMark < m_utteranceMarks.size()
           && m_sent >= m_utteranceMarks.at(m_nextUtteranceMark).position) {
        if (m_utteranceMarks.at(m_nextUtteranceMark++).begins) {
            m_attempts->beginUtterance();
        } else {
            m_attempts->endUtterance();
        }
    }
}

// The current stream takes the input again from position, with the
// utterance marks from there on.
void SpeechStream::rewindTo(qsizetype position)
{
    m_sent = position;
    m_nextUtteranceMark = marksBefore(m_utteranceMarks, position);
}

void SpeechStream::keepAudioBounded()
{
    const qsizetype excess = m_pcm.size() - m_mostKeptBytes;
    if (m_mostKeptBytes == 0 || excess <= 0) {
        return;
    }
    forgetAudioBefore(m_pcmDropped + excess);
    emit audioLost(excess * 1000 / kSpeechBytesPerSecond);
}

// Audio before position is never sent again, nor the utterance marks within
// it.
void SpeechStream::forgetAudioBefore(qsizetype position)
{
    m_pcm.remove(0, position - m_pcmDropped);
    m_pcmDropped = position;
    m_sent = std::max(m_sent, position);
    const qsizetype passed = marksBefore(m_utteranceMarks, position);
    m_utteranceMarks.remove(0, passed);
    m_nextUtteranceMark = std::max<qsizetype>(0, m_nextUtteranceMark - passed);
}

void SpeechStream::handleCompleted()
{
    // The finals were the transcript as they came, and a stream that heard
    // nothing still ended well.
    if (m_inputFinished) {
        emit finished();
        return;
    }
    // The provider ended a stream before the input did (a clean server close).
    if (!m_attempts->takeRollover()) {
        handleFailure(m_attempts->endedTooSoon());
        return;
    }
    qInfo() << "speech stream ended by the provider; rolling over";
    if (m_rollover) {
        m_rollover();
    } else {
        startNextAttempt();
    }
}

void SpeechStream::handleFailure(const SpeechFailure &failure)
{
    if (m_recovery && m_recovery(failure)) {
        return;
    }
    // Nothing reached a service yet. Then the next provider may take the
    // input instead, from the oldest audio kept; after, never: what was sent
    // is not sent again.
    const bool nothingReachedService = m_attempts->nothingReachedService(failure);
    const ProviderAttemptIssue issue{ProviderRole::Speech, m_attempts->chain()->currentId(),
                                     nothingReachedService ? Stage::Connect : Stage::Interrupted, failure.kind,
                                     failure.message};
    if (nothingReachedService && m_attempts->chain()->fallbackRemains(failure.kind)) {
        hold();
        m_issues.append(issue);
        m_attempts->release();
        m_attempts->chain()->prepareNext(m_settings);
        return;
    }
    if (!m_inputFinished && m_attempts->takeReconnect(failure)) {
        qInfo().noquote() << "speech stream dropped, reconnecting: " + failure.message;
        startNextAttempt();
        return;
    }
    m_issues.append(issue);
    // Words the provider did return are the input's result.
    emit failed(m_transcript->isEmpty() ? m_attempts->failureText(failure.message, m_issues) : failure.message);
}

} // namespace speecher
