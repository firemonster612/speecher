#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#ifdef SPEECHER_WITH_QT_WEBSOCKETS
#include <QWebSocket>
#endif

namespace speecher {

QUrlQuery claudeVoiceStreamQuery(const QStringList &vocabulary);
QByteArray claudeVoiceKeytermsHeader(const QStringList &vocabulary);

enum class ClaudeVoiceEventKind {
    Unknown,
    Working,
    Endpoint,
    TranscriptError,
    Error,
};

struct ClaudeVoiceEvent {
    ClaudeVoiceEventKind kind = ClaudeVoiceEventKind::Unknown;
    QString data;
    QString errorSummary;
};

ClaudeVoiceEvent parseClaudeVoiceEvent(const QString &message);

class ClaudeVoiceClient : public QObject {
    Q_OBJECT

public:
    explicit ClaudeVoiceClient(QObject *parent = nullptr);

    void start(const QUrl &url, const QString &accessToken, const QStringList &vocabulary);
    void sendAudio(const QByteArray &pcm);
    void stop();
    void cancel();
    bool isConnected() const;

signals:
    void partialTranscript(const QString &text);
    void finalTranscript(const QString &text);
    void completed();
    void connected();
    void closed();
    void failed(const QString &message, bool retryable, const QString &phase);
    void debugSchema(const QString &message);

private:
    void sendInit(const QStringList &vocabulary);
    void queueAudio(const QByteArray &pcm);
    void flushPendingAudio();
    void clearPendingAudio();
    void requestFinalization();
    void fail(const QString &message, bool retryable, const QString &phase);
    void handleTextMessage(const QString &message);

#ifdef SPEECHER_WITH_QT_WEBSOCKETS
    QWebSocket m_socket;
#endif
    QTimer m_keepAliveTimer;
    QList<QByteArray> m_pendingAudio;
    QString m_lastInterim;
    qsizetype m_pendingAudioBytes = 0;
    bool m_finishRequested = false;
    bool m_finalizing = false;
    bool m_completed = false;
    bool m_cancelled = false;
    bool m_failureEmitted = false;
    bool m_connected = false;
    bool m_debugSchema = false;
    quint64 m_sessionId = 0;
};

} // namespace speecher
