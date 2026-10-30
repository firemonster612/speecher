#include "providers/SpeechAttempts.h"

#include "core/ProviderAttemptIssueText.h"
#include "core/TranscriptState.h"
#include "providers/ProviderRegistry.h"

#include <QDebug>

#include <utility>

namespace speecher {

SpeechAttempts::SpeechAttempts(ProviderRegistry *providers,
                               Transcribers transcribers,
                               TranscriptState *transcript,
                               QObject *parent)
    : QObject(parent)
    , m_providers(providers)
    , m_transcribers(transcribers)
    , m_transcript(transcript)
    , m_chain(new SpeechChain(
          providers, [this](const QString &providerId) { return acquire(providerId); }, this))
{
    // Connected first, so the provider is set up before the session hears
    // that it is ready.
    connect(m_chain, &SpeechChain::ready, this, [this] {
        m_policy.refill();
        m_providerAttempts = 0;
        m_providerConnected = false;
    });
}

SpeechChain *SpeechAttempts::chain() const
{
    return m_chain;
}

SpeechTranscriber *SpeechAttempts::acquire(const QString &providerId)
{
    if (m_transcribers == Transcribers::Fresh) {
        release();
        m_transcriber = m_providers->createSpeechProvider(providerId, this);
    } else {
        disconnectTranscriber();
        m_transcriber = m_providers->speechProvider(providerId);
    }
    if (m_transcriber) {
        connectTranscriber();
    }
    return m_transcriber;
}

// The id moves on first: cancelling can make a provider emit at once.
void SpeechAttempts::release()
{
    if (!m_transcriber) {
        return;
    }
    cancel();
    disconnectTranscriber();
    if (m_transcribers == Transcribers::Fresh) {
        m_transcriber->deleteLater();
    }
    m_transcriber = nullptr;
}

SpeechTranscriber *SpeechAttempts::transcriber() const
{
    return m_transcriber;
}

void SpeechAttempts::setAcceptsText(std::function<bool()> acceptsText)
{
    m_acceptsText = std::move(acceptsText);
}

quint64 SpeechAttempts::currentId() const
{
    return m_id;
}

bool SpeechAttempts::startAttempt(const SpeechSettings &speech)
{
    commitPartial();
    m_transcript->startAttempt(speech.language);
    m_attemptConnected = false;
    m_attemptSentAudio = false;
    ++m_providerAttempts;
    m_policy.attemptStarted();
    const quint64 attemptId = ++m_id;
    m_transcriber->startAttempt(attemptId, speech);
    return attemptId == m_id;
}

void SpeechAttempts::cancel()
{
    if (m_transcriber) {
        m_transcriber->cancelAttempt(m_id++);
    }
}

void SpeechAttempts::forgetAttempt()
{
    ++m_id;
}

bool SpeechAttempts::sendAudio(const QByteArray &pcm)
{
    const quint64 attemptId = m_id;
    m_attemptSentAudio = true;
    m_transcriber->sendAudio(attemptId, pcm);
    return attemptId == m_id;
}

void SpeechAttempts::finishInput()
{
    m_transcriber->finishInput(m_id);
}

void SpeechAttempts::beginUtterance()
{
    m_transcriber->beginUtterance(m_id);
}

void SpeechAttempts::endUtterance()
{
    m_transcriber->endUtterance(m_id);
}

void SpeechAttempts::commitPartial()
{
    const QString partial = m_transcript->partial();
    if (partial.isEmpty()) {
        return;
    }
    m_transcript->commitFinal(partial);
    emit finalized(partial);
}

bool SpeechAttempts::providerConnected() const
{
    return m_providerConnected;
}

bool SpeechAttempts::attemptConnected() const
{
    return m_attemptConnected;
}

bool SpeechAttempts::attemptWasHealthy() const
{
    return m_attemptConnected && m_policy.attemptWasStable();
}

bool SpeechAttempts::nothingReachedService(const SpeechFailure &failure) const
{
    return m_providerAttempts == 1 && !m_attemptConnected
        && (!m_attemptSentAudio || failure.phase == SpeechPhase::Connect);
}

bool SpeechAttempts::takeReconnect(const SpeechFailure &failure)
{
    if (!failure.retryable || failure.phase != SpeechPhase::Streaming) {
        return false;
    }
    m_policy.refillIfAttemptWasStable();
    return m_policy.takeReconnect();
}

bool SpeechAttempts::takeRollover()
{
    if (!m_policy.attemptWasStable()) {
        return false;
    }
    m_policy.refill();
    return true;
}

SpeechFailure SpeechAttempts::endedTooSoon() const
{
    return SpeechAttemptPolicy::endedTooSoon(m_id);
}

bool SpeechAttempts::triedFallbacks(const QList<ProviderAttemptIssue> &issues) const
{
    return m_chain->providerIds().size() > 1 && !issues.isEmpty();
}

QString SpeechAttempts::failureText(const QString &providerMessage, const QList<ProviderAttemptIssue> &issues) const
{
    if (!triedFallbacks(issues)) {
        return providerMessage;
    }
    return noSpeechServiceText(issues, m_providers->labels());
}

void SpeechAttempts::connectTranscriber()
{
    m_connections << connect(m_transcriber, &SpeechTranscriber::attemptConnected, this, [this](quint64 attemptId) {
        if (attemptId == m_id) {
            markConnected();
        }
    });
    m_connections << connect(m_transcriber, &SpeechTranscriber::partialTranscript, this,
                             [this](quint64 attemptId, const QString &text) {
                                 if (takesText(attemptId)) {
                                     m_transcript->setPartial(text);
                                 }
                             });
    m_connections << connect(m_transcriber, &SpeechTranscriber::finalTranscript, this,
                             [this](quint64 attemptId, const QString &text) {
                                 if (takesText(attemptId)) {
                                     m_transcript->commitFinal(text);
                                     emit finalized(text);
                                 }
                             });
    m_connections << connect(m_transcriber, &SpeechTranscriber::attemptTranscript, this,
                             [this](quint64 attemptId, const QString &text) {
                                 if (takesText(attemptId)) {
                                     m_transcript->replaceAttemptFinals(text);
                                 }
                             });
    m_connections << connect(m_transcriber, &SpeechTranscriber::attemptSegments, this,
                             [this](quint64 attemptId, const QList<TranscriptSegment> &timed) {
                                 if (attemptId == m_id) {
                                     emit segments(timed);
                                 }
                             });
    m_connections << connect(m_transcriber, &SpeechTranscriber::utteranceFailed, this,
                             [this](const SpeechFailure &failure) {
                                 if (failure.attemptId == m_id) {
                                     emit utteranceFailed(failure.message);
                                 }
                             });
    m_connections << connect(m_transcriber, &SpeechTranscriber::attemptCompleted, this, [this](quint64 attemptId) {
        if (attemptId == m_id) {
            emit completed();
        }
    });
    m_connections << connect(m_transcriber, &SpeechTranscriber::failed, this, [this](const SpeechFailure &failure) {
        if (failure.attemptId != m_id) {
            qInfo() << "ignored failure from retired speech attempt" << failure.attemptId;
            return;
        }
        emit failed(failure);
    });
}

void SpeechAttempts::disconnectTranscriber()
{
    for (const QMetaObject::Connection &connection : std::as_const(m_connections)) {
        disconnect(connection);
    }
    m_connections.clear();
}

void SpeechAttempts::markConnected()
{
    if (std::exchange(m_attemptConnected, true)) {
        return;
    }
    m_providerConnected = true;
    emit connected();
}

// Text marks the attempt connected even when the session no longer takes
// it.
bool SpeechAttempts::takesText(quint64 attemptId)
{
    if (attemptId != m_id) {
        return false;
    }
    markConnected();
    return !m_acceptsText || m_acceptsText();
}

} // namespace speecher
