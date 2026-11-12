#pragma once

#include "providers/GptLiveSession.h"

#include <QElapsedTimer>
#include <QFile>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
#include <QWebSocket>

#include <memory>

namespace rtc {
class PeerConnection;
class Track;
} // namespace rtc

namespace speecher {

// A GPT-Live call as the ChatGPT sign-in makes one (docs/research/0010): a
// WebRTC peer that sends the caller's audio as PCMU, the call created with its
// offer over HTTPS, and the sideband websocket that brings every event and the
// assistant's audio. The peer takes nothing in: what it would hear comes over
// the sideband as PCM.
class GptLiveCall final : public GptLiveSession {
    Q_OBJECT

public:
    explicit GptLiveCall(QObject *parent = nullptr);
    ~GptLiveCall() override;

    void start(const QString &accessToken, const QString &accountId,
               const GptLiveSessionOptions &options) override;
    void sendAudio(const QByteArray &pcm16k) override;
    qint64 queuedMs() const override;
    void sendContext(const QString &text) override;
    void setInputPaused(bool paused) override;
    void close() override;

private:
    // Creates the peer and its offer; each dial has a number, so what a
    // peer hung up on says reaches nothing.
    void dial();
    void hangUpForRedial();
    void offerReady(quint64 dial, const QString &sdp);
    void callCreated(QNetworkReply *reply);
    // The first 2 s of echoes tell a session that runs at half speed.
    void takeEcho();
    void judgeSpeed(bool slow);
    void sendControl(const QByteArray &message);
    void sendFrames();
    void fail(const QString &message);
    void finish();
    // With SPEECHER_GPT_LIVE_TRACE set: a line for each sideband message,
    // its long audio cut short, for reading the protocol off a run.
    void traceMessage(const char *direction, const QByteArray &message);

    QString m_accessToken;
    QString m_accountId;
    GptLiveSessionOptions m_options;
    QNetworkAccessManager m_network;
    QWebSocket m_sideband;
    std::shared_ptr<rtc::PeerConnection> m_peer;
    std::shared_ptr<rtc::Track> m_track;
    // The caller's audio as PCMU, 8000 bytes a second, waiting for its turn.
    QByteArray m_pcmu;
    Downsampler16kTo8k m_downsampler;
    // Frames go out at the pace they were spoken from when the track opens;
    // silence fills in while nothing waits.
    QTimer m_pacer;
    QElapsedTimer m_since;
    qint64 m_framesSent = 0;
    // From start() to session.started, and from close() to goodbye.
    QTimer m_deadline;
    bool m_started = false;
    // SessionStarted went to the caller: the session is fast enough.
    bool m_ready = false;
    quint64 m_dial = 0;
    int m_redials = 0;
    bool m_hangingUp = false;
    int m_probeEchoes = 0;
    QElapsedTimer m_probe;
    // A session that echoes too little to judge is slow too.
    QTimer m_probeDeadline;
    bool m_closing = false;
    bool m_finished = false;
    QFile m_trace;
    QElapsedTimer m_traceClock;
};

} // namespace speecher
