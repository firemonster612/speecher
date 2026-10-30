#pragma once

#include <QDBusContext>
#include <QDBusObjectPath>
#include <QHash>
#include <QObject>
#include <QVariantMap>

class QDBusPendingCallWatcher;
class QTimer;

namespace speecher {

inline constexpr auto portalService = "org.freedesktop.portal.Desktop";
inline constexpr auto portalPath = "/org/freedesktop/portal/desktop";

// One request to the desktop portal: the call that opens an
// org.freedesktop.portal.Request, the Response it ends with, a deadline, and
// Close when it is withdrawn. Single use; it deletes itself once finished.
class PortalRequest final : public QObject, protected QDBusContext {
    Q_OBJECT

public:
    explicit PortalRequest(QObject *parent = nullptr);

    // Calls `method` on the portal's `interface`. The last of `arguments` is
    // the options vardict, which gains the request's handle token. Exactly
    // one of answered, failed or timedOut follows, possibly before this
    // returns.
    void send(const QString &interface, const QString &method, QVariantList arguments, int timeoutMs);

    // Withdraws an unfinished request: the portal closes it, even one whose
    // handle hasn't arrived yet, and no signal follows. Does nothing once
    // the request has finished.
    void cancel();

signals:
    // The portal's Response: 0 for success, 1 when the person cancelled,
    // 2 for any other end.
    void answered(uint response, const QVariantMap &results);
    // The call failed; `error` is D-Bus's message.
    void failed(const QString &error);
    // No Response came in time, so the request was closed.
    void timedOut();

private slots:
    void handleResponse(uint response, const QVariantMap &results);

private:
    struct Response {
        uint status = 0;
        QVariantMap results;
    };

    void handleReply();
    void finish();

    // Predicted from the handle token until the call's reply names it.
    QDBusObjectPath m_path;
    // The call opening the request, until its reply arrives.
    QDBusPendingCallWatcher *m_call = nullptr;
    // Responses at other paths while the handle is unknown: a portal older
    // than 0.9 picks its own path and can answer before replying with it.
    QHash<QString, Response> m_earlyResponses;
    QTimer *m_deadline;
    bool m_finished = false;
};

} // namespace speecher
