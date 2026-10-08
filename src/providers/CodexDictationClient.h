#pragma once

#include "core/ProviderFailure.h"

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QTimer>
#include <QSet>
#include <QUrl>

#ifdef SPEECHER_WITH_QT_WEBSOCKETS
#include <QWebSocket>
#endif

namespace speecher {

// The chatgpt.com backend is guarded as a browser client. Match the Linux
// Codex client's Chromium user agent so requests reach the dictation and
// transcription services instead of Cloudflare's HTML browser challenge.
inline constexpr auto codexBrowserUserAgent =
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/144.0.0.0 Safari/537.36";

class CodexDictationClient final : public QObject {
    Q_OBJECT

public:
    // The service ends a session once session_ttl_ms passes with no audio
    // (expires_at_ms = last_activity_ms + session_ttl_ms, and every
    // audio.append moves last_activity_ms), and it says nothing when that
    // happens: the next audio.append gets session_not_found. A live stream
    // that has sent no audio for idleKeepAliveMs (the microphone's voice
    // gate holds silence back) sends 100 ms of silence, so a pause longer
    // than the TTL can't swallow the first words after it. Half the TTL
    // leaves minutes of slack for a late timer.
    static constexpr int sessionTtlMs = 300000;
    static constexpr int idleKeepAliveMs = sessionTtlMs / 2;

    // closeTimeoutMs: how long the service may go quiet after the client
    // asks to close before the stream counts as failed.
    explicit CodexDictationClient(QObject *parent = nullptr,
                                  int closeTimeoutMs = 8000,
                                  int keepAliveMs = idleKeepAliveMs);

    // spokenLanguage is a SpeechSettings::language; Automatic sends none,
    // which the service takes as detect.
    void start(const QUrl &url, const QString &accessToken, int sampleRateHz, const QString &spokenLanguage);
    void sendAudio(const QByteArray &pcm);
    // Asks for the final of the utterance sent so far with an audio.flush and
    // keeps the session open. The server's VAD does not end an utterance at a
    // silence, and sends transcript.final only after a flush
    // (docs/research/0004). Before session.started, the flush follows the
    // audio that waits for it.
    void endUtterance();
    void stop();
    void cancel();

signals:
    void partialTranscript(const QString &text);
    void finalTranscript(const QString &text);
    void completed();
    void connected();
    void closed();
    void failed(const QString &message, bool retryable, const QString &phase,
                speecher::ProviderFailureKind kind);

private:
    void sendSessionStart(int sampleRateHz, const QString &language);
    void sendAudioMessage(const QByteArray &pcm);
    void flushPendingAudio();
    void sendAudioFlush();
    void requestFinalization();
    void extendCloseWait();
    void handleTextMessage(const QString &message);
    void fail(const QString &message, bool retryable, const QString &phase, ProviderFailureKind kind);

#ifdef SPEECHER_WITH_QT_WEBSOCKETS
    QWebSocket m_socket;
    QTimer m_closeTimer;
    QTimer m_keepAliveTimer;
#endif
    // What waits for session.started, in order: audio, and an empty entry for
    // each endUtterance().
    QList<QByteArray> m_pendingAudio;
    QSet<QString> m_finalUtteranceIds;
    bool m_sessionStarted = false;
    bool m_finishRequested = false;
    bool m_finalizing = false;
    bool m_sessionClosed = false;
    bool m_cancelled = false;
    bool m_failureEmitted = false;
    quint64 m_sessionId = 0;
};

} // namespace speecher
