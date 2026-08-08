#pragma once

#include "test_prelude.h"

namespace speecher::test {

inline int httpContentLength(const QByteArray &headers)
{
    for (const QByteArray &line : headers.split('\n')) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.toLower().startsWith("content-length:")) {
            bool ok = false;
            const int value = trimmed.mid(QByteArrayLiteral("content-length:").size()).trimmed().toInt(&ok);
            return ok ? value : -1;
        }
    }
    return -1;
}

inline QByteArray readHttpRequest(QTcpSocket *socket, int timeoutMs)
{
    QByteArray request;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        request += socket->readAll();

        const int headerEnd = request.indexOf("\r\n\r\n");
        if (headerEnd >= 0) {
            const int contentLength = httpContentLength(request.left(headerEnd));
            if (contentLength >= 0 && request.size() >= headerEnd + 4 + contentLength) {
                return request;
            }
        }

        socket->waitForReadyRead(20);
    }
    request += socket->readAll();
    return request;
}

} // namespace speecher::test
