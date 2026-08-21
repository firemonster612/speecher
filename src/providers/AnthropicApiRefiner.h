#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>
#include <QTimer>

class QNetworkReply;

namespace speecher {

struct RefinementContext;

class AnthropicApiRefiner final : public QObject {
    Q_OBJECT

public:
    explicit AnthropicApiRefiner(QObject *parent = nullptr, int requestTimeoutMs = 20000);

    void refine(const QString &rawTranscript,
                const QStringList &vocabulary,
                const QStringList &bindingVocabulary,
                const QString &bearerToken,
                const QString &endpointBase,
                const QString &model,
                const QString &effort,
                const QString &refinementStyle,
                const RefinementContext &context);
    void cancel();

signals:
    void delta(const QString &text);
    void completed(const QString &text);
    void failed(const QString &message);

private:
    void parseSseChunk(const QByteArray &chunk);
    void completeIfReady();

    QNetworkAccessManager m_network;
    QTimer m_inactivityTimer;
    QTimer m_deadlineTimer;
    QNetworkReply *m_reply = nullptr;
    int m_requestTimeoutMs;
    QByteArray m_buffer;
    QString m_accumulated;
    bool m_failed = false;
    bool m_completed = false;
};

} // namespace speecher
