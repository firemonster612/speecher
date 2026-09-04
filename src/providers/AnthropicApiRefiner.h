#pragma once

#include <QNetworkAccessManager>
#include <QDeadlineTimer>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include <functional>

class QNetworkReply;

namespace speecher {

struct RefinementContext;

class AnthropicApiRefiner final : public QObject {
    Q_OBJECT

public:
    explicit AnthropicApiRefiner(QObject *parent = nullptr,
                                 int requestTimeoutMs = 20000,
                                 int absoluteDeadlineMs = 120000);

    void refine(const QString &rawTranscript,
                const QStringList &vocabulary,
                const QStringList &bindingVocabulary,
                const QString &bearerToken,
                const QString &endpointBase,
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
    int m_requestTimeoutMs;
    int m_absoluteDeadlineMs;
    QByteArray m_buffer;
    QString m_accumulated;
    bool m_failed = false;
    bool m_completed = false;
};

} // namespace speecher
