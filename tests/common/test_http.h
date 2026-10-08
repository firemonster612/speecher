#pragma once

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

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

inline QByteArray httpResponse(const QByteArray &status, const QByteArray &contentType, const QByteArray &body)
{
    return "HTTP/1.1 " + status + "\r\nContent-Type: " + contentType + "\r\nContent-Length: "
        + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
}

// Answers each request with the route's canned response and records the
// requests it saw. It never blocks the GUI thread: Qt's HTTP thread gets an
// upload's body from this thread, so a blocking read here stalls the request
// it is waiting for.
class FakeServer : public QObject {
public:
    FakeServer()
    {
        QVERIFY(m_server.listen(QHostAddress::LocalHost));
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    if (hasWholeRequest(socket)) answer(socket);
                });
            }
        });
    }

    // Responses for one route are used in order; the last one repeats.
    void route(const QByteArray &methodAndPath, const QByteArray &response)
    {
        m_routes[methodAndPath] << response;
    }

    QString origin() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }

    QJsonObject jsonBody(int index) const
    {
        const QByteArray &request = requests.at(index);
        return QJsonDocument::fromJson(request.mid(request.indexOf("\r\n\r\n") + 4)).object();
    }

    QList<QByteArray> requests;

private:
    void answer(QTcpSocket *socket)
    {
        const QByteArray request = socket->readAll();
        requests << request;
        const QByteArray line = request.left(request.indexOf("\r\n"));
        const QByteArray route = line.split(' ').value(0) + ' ' + line.split(' ').value(1);
        const auto next = [this, &route] {
            QList<QByteArray> &queued = m_routes[route];
            return queued.size() > 1 ? queued.takeFirst() : queued.value(0);
        };
        const QByteArray response = m_routes.contains(route)
            ? next()
            : httpResponse("404 Not Found", "text/plain", "404 page not found for [" + line + "]");
        socket->write(response);
        socket->flush();
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QByteArray, QList<QByteArray>> m_routes;
};

} // namespace speecher::test
