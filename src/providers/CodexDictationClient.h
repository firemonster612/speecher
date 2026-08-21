#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QSet>
#include <QUrl>

#ifdef SPEECHER_WITH_QT_WEBSOCKETS
#include <QWebSocket>
#endif

namespace speecher {

class CodexDictationClient final : public QObject {
    Q_OBJECT

public:
    explicit CodexDictationClient(QObject *parent = nullptr);

    void start(const QUrl &url, const QString &accessToken, int sampleRateHz);
    void sendAudio(const QByteArray &pcm);
    void stop();
    void cancel();
    bool isConnected() const;

signals:
    void finalTranscript(const QString &text);
    void completed();
    void connected();
    void closed();
    void failed(const QString &message, bool retryable, const QString &phase);

private:
    void sendSessionStart(int sampleRateHz);
    void sendAudioMessage(const QByteArray &pcm);
    void flushPendingAudio();
    void requestFinalization();
    void handleTextMessage(const QString &message);
    void fail(const QString &message, bool retryable, const QString &phase);

#ifdef SPEECHER_WITH_QT_WEBSOCKETS
    QWebSocket m_socket;
#endif
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
