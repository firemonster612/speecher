#pragma once

#include <QObject>
#include <QDeadlineTimer>
#include <QNetworkAccessManager>
#include <QStringList>
#include <QTimer>

#include <functional>

namespace speecher {

struct RefinementContext;

class OpenAiRefiner : public QObject {
    Q_OBJECT

public:
    explicit OpenAiRefiner(QObject *parent = nullptr,
                           int requestTimeoutMs = 20000,
                           int absoluteDeadlineMs = 120000);

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
                bool fastMode,
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
    bool retryWithoutFastMode(const QString &reason, bool latchWhenStandardSucceeds);

    QNetworkAccessManager m_network;
    std::function<void()> m_fastModeFallback;
    bool m_fastModePendingLatch = false;
    bool m_fastModeUnavailable = false;
    bool m_retryingFastMode = false;
    QTimer m_inactivityTimer;
    QTimer m_deadlineTimer;
    QDeadlineTimer m_operationDeadline;
    QNetworkReply *m_reply = nullptr;
    int m_requestTimeoutMs = 20000;
    int m_absoluteDeadlineMs = 120000;
    QByteArray m_buffer;
    QString m_accumulated;
    bool m_failed = false;
    bool m_completed = false;
};

} // namespace speecher
