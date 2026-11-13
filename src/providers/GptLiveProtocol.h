#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QMetaType>
#include <QNetworkRequest>
#include <QString>
#include <QStringList>

#include <array>
#include <optional>

namespace speecher {

// GPT-Live as the ChatGPT sign-in reaches it: a WebRTC call created over HTTPS,
// and a websocket beside it with the call's events and the assistant's audio.
// See docs/research/0010-gpt-live-protocol.md.

// The voices a session can start with: ChatGPT Voice's. With the API's own,
// such as marin, the call is created and its session never starts.
QStringList gptLiveVoices();
inline const QString kDefaultGptLiveVoice = QStringLiteral("juniper");

struct GptLiveSessionOptions {
    QString instructions;
    // One of gptLiveVoices(); fixed for the session.
    QString voice = kDefaultGptLiveVoice;
    // GPT-Live runs about a third of its sessions at half speed, too slow to
    // talk with: the call hangs up on one and calls again, unless this.
    bool acceptSlowSession = false;
};

// Whether a session took in 2 s of audio slower than it is spoken: echoMs
// is how long its first 2 s of echoes (11 of them, 200 ms apart) took.
// Real-time sessions take 2.0 s and slow ones 2.5 s or more (docs/research/0010).
inline bool gptLiveSessionIsSlow(qint64 echoMs)
{
    return echoMs > 2400;
}

// The POST that creates a call from a WebRTC offer, and its body. A 201
// answers with the SDP answer as its body and the call id in Location.
QNetworkRequest gptLiveCallRequest(const QString &accessToken, const QString &accountId);
QByteArray gptLiveCallBody(const QString &offerSdp, const GptLiveSessionOptions &options);
// The call id in a created call's Location, or empty when there is none.
QString gptLiveCallId(const QByteArray &location);
// The websocket that carries a call's events and the assistant's audio.
QNetworkRequest gptLiveSidebandRequest(const QString &callId, const QString &accessToken,
                                       const QString &accountId);
// What the sideband takes to end the session; it answers session.closed.
QByteArray gptLiveSessionClose();
// Text for the assistant mid-session, which it acts on as it would on being
// told: the way to steer a session whose instructions are fixed. It answers
// session.context.appended.
QByteArray gptLiveContextAppend(const QString &text);
// Stops or resumes hearing the caller; answered input_audio.paused or
// input_audio.resumed.
QByteArray gptLiveInputPause(bool paused);

// The sideband's 24 kHz mono s16le, the assistant's audio.
inline constexpr int kGptLiveOutputSampleRate = 24000;

// One sideband event, as far as a client needs it.
struct GptLiveEvent {
    enum class Kind {
        // Turn deltas and anything new.
        Ignored,
        SessionStarted,
        // 200 ms of the caller's audio as the session took it in, so a client
        // can tell how fast it does.
        InputEcho,
        // startMs: when a turn began, in session time.
        UserTurnStarted,
        AssistantTurnStarted,
        // A word or two of the user's speech as it is heard.
        UserTranscriptDelta,
        // The user's whole turn.
        UserTurn,
        // The assistant's words as it speaks them.
        AssistantTranscriptDelta,
        AssistantTurn,
        // pcm: the assistant's audio, kGptLiveOutputSampleRate mono s16le.
        // Silence comes too, as zeros.
        AssistantAudio,
        // audioMs: audio the session has heard so far.
        Usage,
        // text: why, e.g. client_request.
        SessionClosed,
        // text: the server's message. The session goes on.
        Error,
        ContextAppended,
        InputPaused,
        InputResumed,
        // Not GPT-Live's own: GptLiveTurnTracker's. text: what the assistant
        // said before the user talked over it; startMs and endMs its turn, so
        // audioMs is how much of it was heard.
        AssistantInterrupted,
    };
    Kind kind = Kind::Ignored;
    QString text;
    qint64 startMs = -1;
    qint64 endMs = -1;
    QByteArray pcm;
    qint64 audioMs = -1;
};

GptLiveEvent parseGptLiveEvent(const QByteArrayView message);

// Tells an assistant turn the user talked over from one that ended. GPT-Live
// cuts its turn short when the user speaks and says nothing more: its turn
// ends and the user's then starts before that end. A back-channel the
// assistant makes while the user speaks overlaps the other way.
class GptLiveTurnTracker {
public:
    // The AssistantInterrupted event, when event shows the last assistant
    // turn was cut short.
    std::optional<GptLiveEvent> take(const GptLiveEvent &event);

private:
    bool m_assistantOpen = false;
    // The assistant turn that just ended, until another turn event comes.
    std::optional<GptLiveEvent> m_justEnded;
    // When the user started while the assistant's turn was open, or -1.
    qint64 m_userStartedDuring = -1;
};

// G.711 mu-law (PCMU), a byte for each signed 16-bit little-endian sample.
QByteArray pcmuFromPcm16(QByteArrayView pcm);

// 16 kHz mono s16le to 8 kHz, low-passed first so nothing above 4 kHz
// folds back. Keeps the last samples of each chunk for the next, so a stream
// can be cut anywhere.
class Downsampler16kTo8k {
public:
    QByteArray process(QByteArrayView pcm16k);

private:
    static constexpr int kTaps = 31;
    std::array<float, kTaps> m_history{};
    // Samples taken in all, so the output keeps every other one.
    qint64 m_taken = 0;
};

} // namespace speecher

Q_DECLARE_METATYPE(speecher::GptLiveEvent)
