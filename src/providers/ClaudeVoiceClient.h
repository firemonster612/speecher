#pragma once

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

class ClaudeVoiceClient : public QObject {
    Q_OBJECT

public:
    explicit ClaudeVoiceClient(QObject *parent = nullptr);

    void start(const QUrl &url, const QString &accessToken, const QStringList &vocabulary);
    void sendAudio(const QByteArray &pcm);
    void stop();
    bool isConnected() const;

signals:
    void partialTranscript(const QString &text);
    void finalTranscript(const QString &text);
    void connected();
    void closed();
    void failed(const QString &message);
    void debugSchema(const QString &message);

private:
    void sendInit(const QStringList &vocabulary);
    void handleTextMessage(const QString &message);

#ifdef SPEECHER_WITH_QT_WEBSOCKETS
    QWebSocket m_socket;
#endif
    QTimer m_keepAliveTimer;
    QString m_lastInterim;
    bool m_finalizing = false;
    bool m_connected = false;
    bool m_debugSchema = false;
    quint64 m_sessionId = 0;
};

} // namespace speecher
