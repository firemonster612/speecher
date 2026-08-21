#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QStringList>
#include <QTimer>

namespace speecher {

struct RefinementContext;

class OpenAiRefiner : public QObject {
    Q_OBJECT

public:
    explicit OpenAiRefiner(QObject *parent = nullptr, int requestTimeoutMs = 20000);

    void refine(const QString &rawTranscript,
                const QStringList &vocabulary,
                const QStringList &bindingVocabulary,
                const QString &bearerToken,
                const QString &organization,
                const QString &project,
                const QString &endpointBase,
                const QString &accountId,
                bool chatgptBackend,
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
    QTimer m_deadlineTimer;
    QNetworkReply *m_reply = nullptr;
    int m_requestTimeoutMs = 20000;
    QByteArray m_buffer;
    QString m_accumulated;
    bool m_failed = false;
    bool m_completed = false;
};

} // namespace speecher
