#include "providers/StreamingRefinement.h"

#include "providers/ServerSentEvents.h"

#include <QDebug>
#include <QNetworkReply>
#include <QPointer>
#include <QScopedValueRollback>

#include <utility>

namespace speecher {

StreamingRefinement::StreamingRefinement(QString provider, DecodeEvent decodeEvent,
                                         DecodeError decodeError, int inactivityMs,
                                         int deadlineMs, QObject *parent)
    : QObject(parent)
    , m_provider(std::move(provider))
    , m_decodeEvent(std::move(decodeEvent))
    , m_decodeError(decodeError)
    , m_inactivityMs(inactivityMs)
    , m_deadlineMs(deadlineMs)
{
    m_inactivityTimer.setSingleShot(true);
    m_deadlineTimer.setSingleShot(true);
    const auto timeout = [this](Retry retry) {
        if (m_reply) {
            fail(m_provider + QStringLiteral(" refinement timed out waiting for a response"), retry);
        }
    };
    connect(&m_inactivityTimer, &QTimer::timeout, this, [timeout] { timeout(Retry::AfterStall); });
    connect(&m_deadlineTimer, &QTimer::timeout, this, [timeout] { timeout(Retry::Never); });
}

void StreamingRefinement::start(BuildRequest buildRequest, const QString &fastTier)
{
    cancel();
    m_operationDeadline = QDeadlineTimer(m_deadlineMs);
    m_latchOnSuccess = false;
    m_fastTier = fastTier;
    const bool fast = !fastTier.isEmpty() && !m_unavailableFastTiers.contains(fastTier);
    if (fast) {
        m_standardFallback = [buildRequest] { return buildRequest(false); };
    }
    post(buildRequest(fast));
}

void StreamingRefinement::post(const Request &request)
{
    m_buffer.clear();
    m_accumulated.clear();
    QNetworkReply *reply = m_network.post(request.headers, request.body);
    m_reply = reply;
    m_inactivityTimer.start(m_inactivityMs);
    // A retry shares the original absolute deadline.
    m_deadlineTimer.start(qMax(1, int(m_operationDeadline.remainingTime())));
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        if (reply == m_reply) parseChunk(reply->readAll());
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (reply != m_reply) {
            reply->deleteLater();
            return;
        }
        takeReply();
        QString detail;
        if (reply->error() != QNetworkReply::NoError) {
            detail = m_decodeError(m_buffer + reply->readAll(), reply->errorString());
        } else {
            detail = m_accumulated.isEmpty() ? QStringLiteral("empty response")
                                            : QStringLiteral("stream ended before completion");
        }
        const QString message = m_provider + QStringLiteral(" refinement failed: ") + detail;
        reply->deleteLater();
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (!retryAtStandardSpeed(message, true)) emit failed(message, httpStatus);
    });
}

QNetworkReply *StreamingRefinement::takeReply()
{
    // Stop suspended parsers, and invalidate queued completion even if its
    // reply was already detached when cancel() was called.
    ++m_generation;
    m_inactivityTimer.stop();
    m_deadlineTimer.stop();
    return std::exchange(m_reply, nullptr);
}

void StreamingRefinement::cancel()
{
    m_standardFallback = nullptr;
    if (QNetworkReply *reply = takeReply()) {
        if (m_parsing) {
            QMetaObject::invokeMethod(reply, &QNetworkReply::abort, Qt::QueuedConnection);
        } else {
            reply->abort();
        }
    }
}

void StreamingRefinement::parseChunk(const QByteArray &chunk)
{
    const QScopedValueRollback parsing(m_parsing, true);
    const quint64 generation = m_generation;
    m_buffer += chunk;
    while (const std::optional<SseFrame> frame = takeSseFrame(m_buffer)) {
        const Event event = m_decodeEvent(frame->name, frame->data);
        switch (event.kind) {
        case Event::Ignore:
            break;
        case Event::Progress:
            m_inactivityTimer.start(m_inactivityMs);
            break;
        case Event::Delta:
            m_inactivityTimer.start(m_inactivityMs);
            m_accumulated += event.text;
            emit delta(event.text);
            if (generation != m_generation) return;
            break;
        case Event::Complete:
            complete();
            return;
        case Event::Rejected:
            fail(event.text, Retry::AfterRejection);
            return;
        case Event::Failed:
            fail(event.text, Retry::Never);
            return;
        }
    }
}

void StreamingRefinement::fail(const QString &message, Retry retry)
{
    QPointer<QNetworkReply> reply = takeReply();
    const bool queuedAbort = m_parsing;
    if (reply && !queuedAbort) reply->abort();
    if (retry == Retry::Never || !retryAtStandardSpeed(message, retry == Retry::AfterRejection)) {
        m_standardFallback = nullptr;
        emit failed(message, reply ? reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() : 0);
    }
    // A failure listener may drain deferred deletes or start another request.
    if (reply && queuedAbort) {
        QMetaObject::invokeMethod(reply, &QNetworkReply::abort, Qt::QueuedConnection);
    }
}

bool StreamingRefinement::retryAtStandardSpeed(const QString &reason, bool latchOnSuccess)
{
    const auto fallback = std::exchange(m_standardFallback, nullptr);
    // Output already delivered to listeners must never be replayed.
    if (!fallback || !m_accumulated.isEmpty()) return false;
    qWarning().noquote() << m_provider.toLower()
                        << "fast mode refinement failed, retrying at standard speed:" << reason;
    m_latchOnSuccess = latchOnSuccess;
    post(fallback());
    return true;
}

void StreamingRefinement::complete()
{
    if (m_accumulated.isEmpty()) return;
    QPointer<QNetworkReply> reply = takeReply();
    m_standardFallback = nullptr;
    if (m_latchOnSuccess) {
        m_latchOnSuccess = false;
        m_unavailableFastTiers.insert(m_fastTier);
        qInfo().noquote() << m_provider.toLower()
                         << m_fastTier << "tier rejected but standard succeeded; skipping that tier until restart";
    }
    const QString result = m_accumulated;
    const quint64 generation = m_generation;
    QMetaObject::invokeMethod(this, [this, reply, result, generation] {
        if (reply) reply->abort();
        if (generation == m_generation) emit completed(result);
    }, Qt::QueuedConnection);
}

} // namespace speecher
