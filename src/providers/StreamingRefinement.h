#pragma once

#include "core/ProviderFailure.h"

#include <QDeadlineTimer>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QObject>
#include <QSet>
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
        // Why a Rejected or Failed event failed. An error the server streamed
        // is its own failure unless its code says otherwise.
        ProviderFailureKind failureKind = ProviderFailureKind::Server;
    };
    using DecodeEvent = std::function<Event(const QByteArray &name, const QByteArray &data)>;
    using DecodeError = QString (*)(const QByteArray &body, const QString &fallback);
    using BuildRequest = std::function<Request(bool fast)>;

    // connectMs bounds the time until the request is sent, so an offline
    // provider fails in seconds; a slow first token is the inactivity
    // budget's business.
    StreamingRefinement(QString provider, DecodeEvent decodeEvent, DecodeError decodeError,
                        int inactivityMs, int deadlineMs, QObject *parent = nullptr,
                        int connectMs = 10000);
    // fastTier names the faster tier asked for, such as "priority" or
    // "ultrafast", or is empty for standard speed. A tier the provider refused
    // this session is skipped; the other tiers are still asked for.
    void start(BuildRequest buildRequest, const QString &fastTier);
    void cancel();

signals:
    void delta(const QString &text);
    void completed(const QString &text);
    void failed(const speecher::ProviderFailure &failure);

private:
    enum class Retry { Never, AfterStall, AfterRejection };
    void post(const Request &request);
    void parseChunk(const QByteArray &chunk);
    QNetworkReply *takeReply();
    void fail(ProviderFailure failure, Retry retry);
    bool retryAtStandardSpeed(const QString &reason, bool latchOnSuccess);
    void complete();
    QString failureMessage(const QString &detail) const;

    QString m_provider;
    DecodeEvent m_decodeEvent;
    DecodeError m_decodeError;
    int m_inactivityMs;
    int m_deadlineMs;
    int m_connectMs;
    QNetworkAccessManager m_network;
    QTimer m_connectTimer;
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
    QString m_fastTier;
    QSet<QString> m_unavailableFastTiers;
};

} // namespace speecher
