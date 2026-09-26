#pragma once

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace speecher {

struct HttpResult {
    // 0 when no HTTP response arrived; error then says why.
    int status = 0;
    QByteArray body;
    QString error;
};

// Waits for the reply on a nested event loop, so it runs on a probe thread
// (runProviderProbe), never on the GUI thread.
inline HttpResult blockingGet(const QNetworkRequest &request, int timeoutMs)
{
    QNetworkAccessManager network;
    QNetworkReply *reply = network.get(request);
    QEventLoop loop;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
    watchdog.start(timeoutMs);
    loop.exec();
    HttpResult result;
    if (!reply->isFinished()) {
        reply->abort();
        result.error = QStringLiteral("no answer within %1 s").arg(timeoutMs / 1000.0);
    } else {
        result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        result.body = reply->readAll();
        if (result.status == 0) {
            result.error = reply->errorString();
        }
    }
    reply->deleteLater();
    return result;
}

} // namespace speecher
