#include "providers/ClaudeVoiceClient.h"

#include "providers/ClaudeCredentials.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QNetworkRequest>
#include <QProcessEnvironment>

#include <utility>

namespace speecher {

namespace {

constexpr qsizetype kMaximumPendingAudioBytes = 4 * 1024 * 1024;

} // namespace

ClaudeVoiceClient::ClaudeVoiceClient(QObject *parent, int connectionTimeoutMs)
    : QObject(parent)
    , m_debugSchema(qEnvironmentVariable("SPEECHER_DEBUG_CLAUDE_SCHEMA") == QStringLiteral("1"))
    , m_connectionTimeoutMs(connectionTimeoutMs)
{
#ifdef SPEECHER_WITH_QT_WEBSOCKETS
    m_connectionTimer.setSingleShot(true);
    connect(&m_connectionTimer, &QTimer::timeout, this, [this] {
        if (!m_connected && !m_cancelled && !m_failureEmitted) {
            fail(QStringLiteral("Claude voice stream timed out while connecting"),
                 true,
                 QStringLiteral("connect"));
            m_socket.abort();
        }
    });
    m_keepAliveTimer.setInterval(8000);
    connect(&m_keepAliveTimer, &QTimer::timeout, this, [this] {
        if (m_socket.state() == QAbstractSocket::ConnectedState) {
            qInfo() << "claude keepalive sent";
            m_socket.sendTextMessage(QStringLiteral("{\"type\":\"KeepAlive\"}"));
        }
    });
    connect(&m_socket, &QWebSocket::connected, this, [this] {
        m_connectionTimer.stop();
        m_connected = true;
        qInfo() << "claude websocket connected";
        qInfo() << "claude initial keepalive sent";
        m_socket.sendTextMessage(QStringLiteral("{\"type\":\"KeepAlive\"}"));
        m_keepAliveTimer.start();
        flushPendingAudio();
        if (m_finishRequested) {
            requestFinalization();
        }
        emit connected();
    });
    connect(&m_socket, &QWebSocket::disconnected, this, [this] {
        m_connectionTimer.stop();
        const bool wasConnected = m_connected;
        const bool unexpected = !m_completed && !m_cancelled && !m_failureEmitted;
        const QString phase = (m_finalizing || m_finishRequested)
            ? QStringLiteral("finalize")
            : wasConnected ? QStringLiteral("streaming") : QStringLiteral("connect");
        m_connected = false;
        m_finalizing = false;
        m_keepAliveTimer.stop();
        clearPendingAudio();
        qInfo() << "claude websocket disconnected";
        if (unexpected) {
            fail(phase == QStringLiteral("finalize")
                     ? QStringLiteral("Claude voice stream closed before final transcript completion")
                     : QStringLiteral("Claude voice stream disconnected unexpectedly"),
                 true,
                 phase);
        }
        emit closed();
    });
    connect(&m_socket, &QWebSocket::textMessageReceived, this, &ClaudeVoiceClient::handleTextMessage);
    connect(&m_socket, &QWebSocket::binaryMessageReceived, this, [this](const QByteArray &message) {
        if (m_debugSchema) {
            emit debugSchema(QStringLiteral("binary:%1").arg(message.size()));
        }
        qInfo() << "claude websocket binary message bytes=" << message.size();
    });
    connect(&m_socket, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        qWarning().noquote() << "claude websocket error=" + m_socket.errorString();
        if (!m_cancelled && !m_failureEmitted) {
            const QString detail = m_socket.errorString();
            const bool authentication = detail.contains(QStringLiteral("401"))
                || detail.contains(QStringLiteral("403"))
                || detail.contains(QStringLiteral("unauthorized"), Qt::CaseInsensitive)
                || detail.contains(QStringLiteral("forbidden"), Qt::CaseInsensitive);
            fail(QStringLiteral("Claude voice API changed or connection failed: %1").arg(detail),
                 !authentication,
                 authentication
                     ? QStringLiteral("authentication")
                     : (m_finalizing ? QStringLiteral("finalize")
                                     : (m_connected ? QStringLiteral("streaming")
                                                    : QStringLiteral("connect"))));
        }
    });
#endif
}

void ClaudeVoiceClient::start(const QUrl &url, const QString &accessToken, const QStringList &vocabulary)
{
#ifdef SPEECHER_WITH_QT_WEBSOCKETS
    QUrl streamUrl(url);
    streamUrl.setQuery(claudeVoiceStreamQuery(vocabulary));

    m_lastInterim.clear();
    m_finishRequested = false;
    m_finalizing = false;
    m_completed = false;
    m_cancelled = false;
    m_failureEmitted = false;
    clearPendingAudio();
    ++m_sessionId;
    QNetworkRequest request(streamUrl);
    request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());
    const QString claudeVersion = ClaudeCredentials::installedVersion();
    request.setRawHeader("User-Agent", claudeVersion.isEmpty()
                                           ? QByteArrayLiteral("Claude-Code")
                                           : QStringLiteral("Claude-Code/%1").arg(claudeVersion).toUtf8());
    request.setRawHeader("x-app", "cli");
    request.setRawHeader("anthropic-client-platform", "linux");
    const QByteArray keyterms = claudeVoiceKeytermsHeader(vocabulary);
    if (!keyterms.isEmpty()) {
        request.setRawHeader("x-config-keyterms", keyterms);
    }
    qInfo().noquote() << "claude websocket opening url=" + streamUrl.toString(QUrl::RemoveUserInfo)
                      << "vocabularyCount=" + QString::number(vocabulary.size());
    m_socket.open(request);
    m_connectionTimer.start(m_connectionTimeoutMs);
#else
    Q_UNUSED(url)
    Q_UNUSED(accessToken)
    Q_UNUSED(vocabulary)
    emit failed(QStringLiteral("Qt WebSockets support was not built; install Qt6 WebSockets development files and rebuild"),
                false,
                QStringLiteral("protocol"));
#endif
}

void ClaudeVoiceClient::sendInit(const QStringList &vocabulary)
{
#ifdef SPEECHER_WITH_QT_WEBSOCKETS
    Q_UNUSED(vocabulary)
#else
    Q_UNUSED(vocabulary)
#endif
}

void ClaudeVoiceClient::sendAudio(const QByteArray &pcm)
{
#ifdef SPEECHER_WITH_QT_WEBSOCKETS
    if (m_cancelled || m_failureEmitted || m_completed || m_finishRequested) {
        return;
    }
    if (m_connected && !m_finalizing && !m_finishRequested) {
        m_socket.sendBinaryMessage(pcm);
    } else if (!m_finalizing) {
        queueAudio(pcm);
    }
#else
    Q_UNUSED(pcm)
#endif
}

void ClaudeVoiceClient::stop()
{
#ifdef SPEECHER_WITH_QT_WEBSOCKETS
    m_finishRequested = true;
    if (m_connected) {
        requestFinalization();
    } else {
        const quint64 sessionId = m_sessionId;
        QTimer::singleShot(5000, this, [this, sessionId] {
            if (sessionId == m_sessionId
                && m_finishRequested
                && !m_finalizing
                && !m_completed
                && !m_failureEmitted) {
                fail(QStringLiteral("Claude voice stream did not become ready for finalization"),
                     true,
                     QStringLiteral("connect"));
                m_socket.abort();
            }
        });
        if (m_socket.state() == QAbstractSocket::UnconnectedState && !m_failureEmitted) {
            fail(QStringLiteral("Claude voice stream closed before input could be finalized"),
                 true,
                 QStringLiteral("connect"));
        }
    }
    qInfo() << "claude close requested connected=" << m_connected;
#endif
}

void ClaudeVoiceClient::cancel()
{
#ifdef SPEECHER_WITH_QT_WEBSOCKETS
    m_cancelled = true;
    m_finishRequested = false;
    m_finalizing = false;
    clearPendingAudio();
    m_connectionTimer.stop();
    m_keepAliveTimer.stop();
    m_socket.abort();
#endif
}

bool ClaudeVoiceClient::isConnected() const
{
    return m_connected;
}

void ClaudeVoiceClient::queueAudio(const QByteArray &pcm)
{
#ifdef SPEECHER_WITH_QT_WEBSOCKETS
    if (pcm.isEmpty()) {
        return;
    }

    if (m_pendingAudioBytes + pcm.size() > kMaximumPendingAudioBytes) {
        fail(QStringLiteral("Claude voice audio buffer filled before the stream connected"),
             true,
             QStringLiteral("connect"));
        m_socket.abort();
        return;
    }

    m_pendingAudio.append(pcm);
    m_pendingAudioBytes += pcm.size();
#else
    Q_UNUSED(pcm)
#endif
}

void ClaudeVoiceClient::flushPendingAudio()
{
#ifdef SPEECHER_WITH_QT_WEBSOCKETS
    if (!m_connected || m_finalizing || m_pendingAudio.isEmpty()) {
        return;
    }

    qInfo() << "claude websocket flushing pending audio bytes=" << m_pendingAudioBytes
            << "chunks=" << m_pendingAudio.size();
    for (const QByteArray &pcm : std::as_const(m_pendingAudio)) {
        m_socket.sendBinaryMessage(pcm);
    }
    clearPendingAudio();
#endif
}

void ClaudeVoiceClient::clearPendingAudio()
{
    m_pendingAudio.clear();
    m_pendingAudioBytes = 0;
}

void ClaudeVoiceClient::requestFinalization()
{
#ifdef SPEECHER_WITH_QT_WEBSOCKETS
    if (!m_connected || m_finalizing || m_completed || m_failureEmitted) {
        return;
    }

    flushPendingAudio();
    m_finalizing = true;
    qInfo() << "claude finalize requested";
    m_socket.sendTextMessage(QStringLiteral("{\"type\":\"CloseStream\"}"));
    const quint64 sessionId = m_sessionId;
    QTimer::singleShot(5000, this, [this, sessionId] {
        if (sessionId == m_sessionId
            && m_finalizing
            && !m_completed
            && !m_failureEmitted) {
            fail(QStringLiteral("Claude voice stream timed out while waiting for the final transcript"),
                 true,
                 QStringLiteral("finalize"));
            m_socket.abort();
        }
    });
#endif
}

void ClaudeVoiceClient::fail(const QString &message, bool retryable, const QString &phase)
{
    if (m_cancelled || m_failureEmitted) {
        return;
    }
    m_failureEmitted = true;
    m_connectionTimer.stop();
    m_keepAliveTimer.stop();
    clearPendingAudio();
    emit failed(message, retryable, phase);
}

void ClaudeVoiceClient::handleTextMessage(const QString &message)
{
#ifdef SPEECHER_WITH_QT_WEBSOCKETS
    const QJsonObject object = QJsonDocument::fromJson(message.toUtf8()).object();
    if (m_debugSchema) {
        emit debugSchema(QStringLiteral("text type=%1 keys=%2")
                             .arg(object.value(QStringLiteral("type")).toString(),
                                  object.keys().join(',')));
    }
    qInfo().noquote() << "claude websocket text message type="
                      + object.value(QStringLiteral("type")).toString()
                      + " event=" + object.value(QStringLiteral("event")).toString()
                      + " keys=" + object.keys().join(',');
    const ClaudeVoiceEvent event = parseClaudeVoiceEvent(message);
    if (event.kind == ClaudeVoiceEventKind::Error) {
        qWarning().noquote() << "claude server error " + event.errorSummary;
        const bool authentication = event.errorSummary.contains(QStringLiteral("401"))
            || event.errorSummary.contains(QStringLiteral("403"))
            || event.errorSummary.contains(QStringLiteral("unauthorized"), Qt::CaseInsensitive)
            || event.errorSummary.contains(QStringLiteral("forbidden"), Qt::CaseInsensitive);
        fail(QStringLiteral("Claude voice server error: %1").arg(event.errorSummary),
             !authentication,
             authentication ? QStringLiteral("authentication")
                            : (m_finalizing ? QStringLiteral("finalize")
                                            : QStringLiteral("streaming")));
        return;
    }
    if (event.kind == ClaudeVoiceEventKind::Endpoint) {
        qInfo() << "claude transcript endpoint lastInterimLength=" << m_lastInterim.size();
        if (!event.data.simplified().isEmpty()) {
            m_lastInterim = event.data.simplified();
        }
        if (!m_lastInterim.isEmpty()) {
            emit finalTranscript(m_lastInterim);
            m_lastInterim.clear();
        }
        if (m_finalizing) {
            m_completed = true;
            emit completed();
            m_socket.close();
        }
        return;
    }
    if (event.kind == ClaudeVoiceEventKind::TranscriptError) {
        qWarning().noquote() << "claude transcript error " + event.errorSummary;
        fail(event.errorSummary.isEmpty() ? QStringLiteral("Claude transcript error")
                                          : event.errorSummary,
             true,
             m_finalizing ? QStringLiteral("finalize") : QStringLiteral("streaming"));
        return;
    }
    if (event.kind != ClaudeVoiceEventKind::Working) {
        return;
    }

    const QString text = event.data.simplified();
    if (text.isEmpty()) {
        qInfo() << "claude text message contained no transcript text";
        return;
    }
    qInfo() << "claude partial transcript length=" << text.size();
    m_lastInterim = text;
    emit partialTranscript(text);
#else
    Q_UNUSED(message)
#endif
}

} // namespace speecher
