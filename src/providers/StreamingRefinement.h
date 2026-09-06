#pragma once

#include <QDeadlineTimer>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QObject>
#include <QTimer>

#include <functional>

namespace speecher {

// One HTTP/SSE operation, including its optional standard-speed retry. Providers
// build requests and decode events; this owns the connection and delivery state.
class StreamingRefinement final : public QObject {
    Q_OBJECT

public:
    struct Request {
        QNetworkRequest headers;
        QByteArray body;
    };
    struct Event {
        // Rejected permits a standard-speed retry before output; Failed does not.
        enum Kind { Ignore, Progress, Delta, Complete, Rejected, Failed };
        Kind kind = Ignore;
        QString text;
    };
    using DecodeEvent = Event (*)(const QByteArray &name, const QByteArray &data);
    using DecodeError = QString (*)(const QByteArray &body, const QString &fallback);
    using BuildRequest = std::function<Request(bool fast)>;

    StreamingRefinement(QString provider, DecodeEvent decodeEvent, DecodeError decodeError,
                        int inactivityMs, int deadlineMs, QObject *parent = nullptr);
    void start(BuildRequest buildRequest, bool fastMode);
    void cancel();

signals:
    void delta(const QString &text);
    void completed(const QString &text);
    void failed(const QString &message);

private:
    enum class Retry { Never, AfterStall, AfterRejection };
    void post(const Request &request);
    void parseChunk(const QByteArray &chunk);
    QNetworkReply *takeReply();
    void fail(const QString &message, Retry retry);
    bool retryAtStandardSpeed(const QString &reason, bool latchOnSuccess);
    void complete();

    QString m_provider;
    DecodeEvent m_decodeEvent;
    DecodeError m_decodeError;
    int m_inactivityMs;
    int m_deadlineMs;
    QNetworkAccessManager m_network;
    QTimer m_inactivityTimer;
    QTimer m_deadlineTimer;
    QDeadlineTimer m_operationDeadline;
    QNetworkReply *m_reply = nullptr;
    std::function<Request()> m_standardFallback;
    QByteArray m_buffer;
    QString m_accumulated;
    quint64 m_generation = 0;
    bool m_parsing = false;
    bool m_latchOnSuccess = false;
    bool m_fastModeUnavailable = false;
};

} // namespace speecher
