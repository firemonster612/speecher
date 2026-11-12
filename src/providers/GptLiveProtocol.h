#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QMetaType>
#include <QNetworkRequest>
#include <QString>
#include <QStringList>

#include <array>

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
};

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

// The sideband's 24 kHz mono s16le, the assistant's audio.
inline constexpr int kGptLiveOutputSampleRate = 24000;

// One sideband event, as far as a client needs it.
struct GptLiveEvent {
    enum class Kind {
        // Echoes of the input, and turn bookkeeping a client can do without.
        Ignored,
        SessionStarted,
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
    };
    Kind kind = Kind::Ignored;
    QString text;
    qint64 startMs = -1;
    qint64 endMs = -1;
    QByteArray pcm;
    qint64 audioMs = -1;
};

GptLiveEvent parseGptLiveEvent(const QByteArrayView message);

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
