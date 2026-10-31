#include "recording/RecordingStream.h"

#include "core/TranscriptState.h"
#include "providers/SpeechAudio.h"

#include <QDebug>

#include <utility>

namespace speecher {
namespace {

// The pauses before each reconnect of a dropped stream; the last repeats for
// as long as the input runs.
constexpr int kReconnectDelaysMs[] = {1000, 2000, 5000, 10000, 30000};
// The most audio kept while the stream is down: ten minutes.
constexpr qsizetype kMostKeptBytes = qsizetype(10) * 60 * kSpeechBytesPerSecond;

} // namespace

RecordingStream::RecordingStream(ProviderRegistry *providers, QObject *parent)
    : QObject(parent)
    , m_transcript(new TranscriptState(this))
    , m_stream(new SpeechStream(providers, m_transcript, {.cutIntoUtterances = true}, this))
{
    SpeechAttempts *speech = m_stream->attempts();
    // The provider starts once the worker reports, never inside start().
    speech->chain()->setOptions({.streamedFinalsOnly = true, .readyLater = true});
    m_stream->setMostKeptBytes(kMostKeptBytes);
    m_stream->setRecovery([this](const SpeechFailure &failure) { return reconnectLater(failure); });
    m_stream->setRollover([this] { reconnect(); });
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &RecordingStream::reconnect);
    connect(speech, &SpeechAttempts::connected, this, [this] {
        m_signInRefused = false;
        emit connected();
    });
    connect(speech, &SpeechAttempts::finalized, this, &RecordingStream::textFinalized);
    connect(speech, &SpeechAttempts::utteranceFailed, this, &RecordingStream::utteranceFailed);
    connect(speech->chain(), &SpeechChain::signInRenewed, this, &RecordingStream::handleSignInRenewed);
    connect(m_stream, &SpeechStream::startedOver, this, [this] { m_reconnectDelayIndex = 0; });
    connect(m_stream, &SpeechStream::audioLost, this, &RecordingStream::audioLost);
    connect(m_stream, &SpeechStream::finished, this, &RecordingStream::finish);
    connect(m_stream, &SpeechStream::failed, this, &RecordingStream::fail);
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
    return m_stream->transcriber()->utteranceAnswerTimeoutMs();
}

// The input fills the stream from before the providers prepare.
void RecordingStream::start(AudioInput *input, const AppSettings &settings)
{
    m_settings = settings;
    m_settings.speech.codexFinalRetranscribe = false;
    m_running = true;
    m_error.clear();
    m_stream->reset();
    m_input = input;
    connect(m_input, &AudioInput::audioChunk, this, [this](const QByteArray &pcm) {
        if (m_inputState != Input::Off) {
            m_stream->append(pcm);
        }
    });
    connect(m_input, &AudioInput::started, this, [this] {
        m_inputState = Input::Listening;
        // The recording's own provider fails the stream when it cannot listen
        // for the Spoken Language.
        m_stream->prepare(m_settings);
    });
    // The post-roll arrives before it, and still counts.
    connect(m_input, &AudioInput::stopped, this, [this] {
        endInput();
        emit listeningFinished();
    });
    // An input that drops after it heard something still has that sent.
    connect(m_input, &AudioInput::failed, this, [this](const QString &message) {
        if (!m_stream->hasAudio()) {
            fail(message);
            return;
        }
        m_error = message;
        endInput();
    });
    m_inputState = Input::Starting;
    m_input->start();
}

void RecordingStream::finishListening()
{
    switch (m_inputState) {
    case Input::Listening:
        m_inputState = Input::Stopping;
        m_input->stop();
        return;
    case Input::Starting:
        // Nothing was heard, and no provider prepared.
        emit listeningFinished();
        finish();
        return;
    case Input::Off:
        if (m_running) {
            emit listeningFinished();
        }
        return;
    case Input::Stopping:
        return;
    }
}

void RecordingStream::endInput()
{
    m_inputState = Input::Off;
    m_stream->endInput();
    // A stream waiting to reconnect tries once more, now, for the audio that
    // waits.
    if (m_reconnectTimer.isActive()) {
        m_reconnectTimer.stop();
        reconnect();
    }
}

// Audio the input delivers as it stops can begin one too.
void RecordingStream::beginUtterance(qsizetype voicedBytes)
{
    if (m_inputState != Input::Off) {
        m_stream->beginUtterance(voicedBytes);
    }
}

void RecordingStream::endUtterance()
{
    if (m_running) {
        m_stream->endUtterance();
    }
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

// Once a stream of the provider has connected, one that drops while the
// input runs waits and reconnects.
bool RecordingStream::reconnectLater(const SpeechFailure &failure)
{
    if (!m_stream->attempts()->providerConnected() || m_inputState != Input::Listening) {
        return false;
    }
    // A sign-in the service turns down may only have expired: the stream
    // reconnects once with it renewed before that stops it. A provider with
    // nothing to renew would be turned down again.
    const bool firstRefusal = failure.kind == ProviderFailureKind::Authentication
        && m_stream->transcriber()->renewsSignIn() && !std::exchange(m_signInRefused, true);
    if (!failure.retryable && !firstRefusal) {
        return false;
    }
    waitToReconnect(failure.message);
    return true;
}

// A dropped stream waits a pause, growing while it keeps failing, before it
// reconnects; the input's audio waits with it.
void RecordingStream::waitToReconnect(const QString &reason)
{
    const bool healthy = m_stream->attempts()->attemptWasHealthy();
    m_stream->dropAttempt();
    if (healthy) {
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
    m_stream->hold();
    if (renewSignIn()) {
        return;
    }
    m_stream->resume();
}

// A recording outlasts its sign-in, so the next stream waits for it to renew
// when the provider says it is due, or when the service turned it down.
// Returns whether it is renewing.
bool RecordingStream::renewSignIn()
{
    if (!m_signInRefused
        && !m_stream->transcriber()->requiresRefresh(m_stream->speechSettings(), m_stream->settings().accounts)) {
        return false;
    }
    qInfo() << "recording renews its speech sign-in";
    m_stream->renewSignIn();
    return true;
}

// A renewal that failed fails the stream it was for: one that could not reach
// the service reconnects later, and one turned down stops it.
void RecordingStream::handleSignInRenewed(const std::optional<ProviderFailure> &failure)
{
    if (!failure) {
        m_stream->resume();
        return;
    }
    m_stream->handleFailure({m_stream->attempts()->currentId(), failure->message, isTransientFailure(failure->kind),
                             SpeechPhase::Connect, failure->kind});
}

void RecordingStream::fail(const QString &message)
{
    qWarning().noquote() << "recording stream failed: " + message;
    // The provider will never finish the partial now.
    m_stream->attempts()->commitPartial();
    m_error = message;
    finish();
}

void RecordingStream::finish()
{
    release();
    m_running = false;
    emit finished(m_error);
}

void RecordingStream::release()
{
    m_reconnectTimer.stop();
    m_stream->release();
    m_signInRefused = false;
    if (m_input) {
        disconnect(m_input, nullptr, this, nullptr);
        if (std::exchange(m_inputState, Input::Off) != Input::Off) {
            m_input->abort();
        }
    }
}

} // namespace speecher
