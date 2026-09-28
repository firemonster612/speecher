#pragma once

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTcpSocket>

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

// The socket has buffered the headers and the whole Content-Length body (none
// without the header). Peeks, so the request stays readable.
inline bool hasWholeRequest(QTcpSocket *socket)
{
    const QByteArray buffered = socket->peek(socket->bytesAvailable());
    const int headerEnd = buffered.indexOf("\r\n\r\n");
    return headerEnd >= 0
        && buffered.size() >= headerEnd + 4 + qMax(0, httpContentLength(buffered.left(headerEnd)));
}

inline QByteArray readHttpRequest(QTcpSocket *socket, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs && !hasWholeRequest(socket)) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        socket->waitForReadyRead(20);
    }
    return socket->readAll();
}

} // namespace speecher::test
