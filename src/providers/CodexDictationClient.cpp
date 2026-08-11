#include "providers/CodexDictationClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QTimer>

#include <utility>

#ifdef SPEECHER_WITH_QT_WEBSOCKETS
#include <QWebSocketHandshakeOptions>
#endif

namespace speecher {
namespace {

QString errorMessage(const QJsonObject &event)
{
    return event.value(QStringLiteral("error"))
        .toObject()
        .value(QStringLiteral("message"))
        .toString();
}

bool isAuthenticationError(const QString &message, const QJsonObject &event = {})
{
    const QJsonValue code = event.value(QStringLiteral("error"))
                                .toObject()
                                .value(QStringLiteral("code"));
    const QString codeText = code.toVariant().toString();
    return codeText == QStringLiteral("401") || codeText == QStringLiteral("403")
        || message.contains(QStringLiteral("401"))
        || message.contains(QStringLiteral("403"))
        || message.contains(QStringLiteral("unauthorized"), Qt::CaseInsensitive)
        || message.contains(QStringLiteral("forbidden"), Qt::CaseInsensitive);
}

} // namespace

CodexDictationClient::CodexDictationClient(QObject *parent)
    : QObject(parent)
{
#ifdef SPEECHER_WITH_QT_WEBSOCKETS
    connect(&m_socket, &QWebSocket::connected, this, [this] {
        sendSessionStart(m_socket.property("sampleRateHz").toInt());
    });
    connect(&m_socket, &QWebSocket::textMessageReceived,
            this, &CodexDictationClient::handleTextMessage);
    connect(&m_socket, &QWebSocket::disconnected, this, [this] {
        if (!m_cancelled && !m_sessionClosed && !m_failureEmitted) {
            fail(m_sessionStarted
                     ? QStringLiteral("Codex dictation stream closed before the session finished")
                     : QStringLiteral("Codex dictation stream closed before session.start completed"),
                 true,
                 m_finalizing ? QStringLiteral("finalize")
                              : (m_sessionStarted ? QStringLiteral("streaming")
                                                  : QStringLiteral("connect")));
        }
        emit closed();
    });
    connect(&m_socket, &QWebSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) {
                if (m_cancelled || m_failureEmitted || m_sessionClosed) {
                    return;
                }
                const QString detail = m_socket.errorString();
                const bool authentication = isAuthenticationError(detail);
                fail(QStringLiteral("Codex dictation connection failed: %1").arg(detail),
                     !authentication,
                     authentication
                         ? QStringLiteral("authentication")
                         : (m_finalizing ? QStringLiteral("finalize")
                                         : (m_sessionStarted ? QStringLiteral("streaming")
                                                             : QStringLiteral("connect"))));
            });
#endif
}

void CodexDictationClient::start(const QUrl &url,
                                 const QString &accessToken,
                                 int sampleRateHz)
{
#ifdef SPEECHER_WITH_QT_WEBSOCKETS
    m_pendingAudio.clear();
    m_sessionStarted = false;
    m_finishRequested = false;
    m_finalizing = false;
    m_sessionClosed = false;
    m_cancelled = false;
    m_failureEmitted = false;
    ++m_sessionId;

    m_socket.setProperty("sampleRateHz", sampleRateHz);
    QWebSocketHandshakeOptions options;
    options.setSubprotocols({QStringLiteral("chatgpt-dictation"),
                             QStringLiteral("openai-bearer.%1").arg(accessToken)});
    QNetworkRequest request(url);
    // The production endpoint is guarded as a browser WebSocket. Match the
    // Linux Codex client's Chromium user agent so the upgrade reaches the
    // dictation service instead of Cloudflare's HTML browser challenge.
    request.setHeader(
        QNetworkRequest::UserAgentHeader,
        QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                       "(KHTML, like Gecko) Chrome/144.0.0.0 Safari/537.36"));
    m_socket.open(request, options);

    const quint64 sessionId = m_sessionId;
    QTimer::singleShot(10000, this, [this, sessionId] {
        if (sessionId == m_sessionId
            && !m_sessionStarted
            && !m_cancelled
            && !m_failureEmitted) {
            fail(QStringLiteral("Codex dictation stream timed out before session.start completed"),
                 true,
                 QStringLiteral("connect"));
            m_socket.abort();
        }
    });
#else
    Q_UNUSED(url)
    Q_UNUSED(accessToken)
    Q_UNUSED(sampleRateHz)
    emit failed(QStringLiteral("Qt WebSockets support was not built; install Qt6 WebSockets development files and rebuild"),
                false,
                QStringLiteral("protocol"));
#endif
}

void CodexDictationClient::sendSessionStart(int sampleRateHz)
{
#ifdef SPEECHER_WITH_QT_WEBSOCKETS
    const QJsonObject vad{
        {QStringLiteral("type"), QStringLiteral("server_vad")},
        {QStringLiteral("threshold"), 0.5},
        {QStringLiteral("prefix_padding_ms"), 300},
        {QStringLiteral("silence_duration_ms"), 500},
    };
    const QJsonObject config{
        {QStringLiteral("input_audio_format"), QStringLiteral("pcm16")},
        {QStringLiteral("sample_rate_hz"), sampleRateHz},
        {QStringLiteral("num_channels"), 1},
        {QStringLiteral("max_buffer_size_bytes"), 4 * 1024 * 1024},
        {QStringLiteral("max_utterance_duration_ms"), 30000},
        {QStringLiteral("session_ttl_ms"), 300000},
        {QStringLiteral("provider_mode"), QStringLiteral("streaming_sse")},
        {QStringLiteral("transcript_delivery_mode"), QStringLiteral("final_only")},
        {QStringLiteral("vad"), vad},
    };
    m_socket.sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("session.start")},
        {QStringLiteral("config"), config},
    }).toJson(QJsonDocument::Compact)));
#else
    Q_UNUSED(sampleRateHz)
#endif
}

void CodexDictationClient::sendAudio(const QByteArray &pcm)
{
#ifdef SPEECHER_WITH_QT_WEBSOCKETS
    if (pcm.isEmpty() || m_finishRequested || m_finalizing || m_sessionClosed) {
        return;
    }
    if (!m_sessionStarted) {
        m_pendingAudio.append(pcm);
        return;
    }
    sendAudioMessage(pcm);
#else
    Q_UNUSED(pcm)
#endif
}

void CodexDictationClient::sendAudioMessage(const QByteArray &pcm)
{
#ifdef SPEECHER_WITH_QT_WEBSOCKETS
    m_socket.sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("audio.append")},
        {QStringLiteral("audio"), QString::fromLatin1(pcm.toBase64())},
    }).toJson(QJsonDocument::Compact)));
#else
    Q_UNUSED(pcm)
#endif
}

void CodexDictationClient::flushPendingAudio()
{
    for (const QByteArray &pcm : std::as_const(m_pendingAudio)) {
        sendAudioMessage(pcm);
    }
    m_pendingAudio.clear();
}

void CodexDictationClient::stop()
{
#ifdef SPEECHER_WITH_QT_WEBSOCKETS
    if (m_finishRequested || m_finalizing || m_sessionClosed || m_cancelled) {
        return;
    }
    m_finishRequested = true;
    if (m_sessionStarted) {
        requestFinalization();
    }
#endif
}

void CodexDictationClient::requestFinalization()
{
#ifdef SPEECHER_WITH_QT_WEBSOCKETS
    if (!m_sessionStarted || m_finalizing || m_sessionClosed || m_failureEmitted) {
        return;
    }
    flushPendingAudio();
    m_finalizing = true;
    m_socket.sendTextMessage(QStringLiteral("{\"type\":\"audio.flush\",\"reason\":\"client\"}"));
    m_socket.sendTextMessage(QStringLiteral("{\"type\":\"session.close\"}"));

    const quint64 sessionId = m_sessionId;
    QTimer::singleShot(8000, this, [this, sessionId] {
        if (sessionId == m_sessionId
            && m_finalizing
            && !m_sessionClosed
            && !m_failureEmitted) {
            fail(QStringLiteral("Codex dictation stream timed out while closing the session"),
                 true,
                 QStringLiteral("finalize"));
            m_socket.abort();
        }
    });
#endif
}

void CodexDictationClient::cancel()
{
#ifdef SPEECHER_WITH_QT_WEBSOCKETS
    m_cancelled = true;
    m_pendingAudio.clear();
    m_socket.abort();
#endif
}

bool CodexDictationClient::isConnected() const
{
    return m_sessionStarted && !m_sessionClosed;
}

void CodexDictationClient::handleTextMessage(const QString &message)
{
#ifdef SPEECHER_WITH_QT_WEBSOCKETS
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(message.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        fail(QStringLiteral("Codex dictation stream returned an invalid event payload"),
             false,
             QStringLiteral("protocol"));
        m_socket.close();
        return;
    }

    const QJsonObject event = document.object();
    const QString type = event.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("session.started")) {
        if (!m_sessionStarted) {
            m_sessionStarted = true;
            flushPendingAudio();
            emit connected();
            if (m_finishRequested) {
                requestFinalization();
            }
        }
        return;
    }
    if (type == QStringLiteral("session.updated")
        && event.value(QStringLiteral("session")).toObject()
               .value(QStringLiteral("status")).toString() == QStringLiteral("closed")) {
        if (!m_sessionClosed) {
            m_sessionClosed = true;
            emit completed();
            m_socket.close();
        }
        return;
    }
    if (type == QStringLiteral("transcript.final")) {
        const QString text = event.value(QStringLiteral("text")).toString().trimmed();
        if (!text.isEmpty()) {
            emit finalTranscript(text);
        }
        return;
    }
    if (type == QStringLiteral("transcript.failed")) {
        const QJsonObject error = event.value(QStringLiteral("error")).toObject();
        const QString message = error.value(QStringLiteral("message")).toString(
            QStringLiteral("Codex dictation failed to transcribe an utterance"));
        const bool authentication = isAuthenticationError(message, event);
        fail(message,
             !authentication && error.value(QStringLiteral("retryable")).toBool(true),
             authentication ? QStringLiteral("authentication")
                            : (m_finalizing ? QStringLiteral("finalize")
                                            : QStringLiteral("streaming")));
        m_socket.close();
        return;
    }
    if (type == QStringLiteral("session.error")
        && event.value(QStringLiteral("fatal")).toBool()) {
        const QString message = errorMessage(event).isEmpty()
            ? QStringLiteral("Codex dictation session failed")
            : errorMessage(event);
        const bool authentication = isAuthenticationError(message, event);
        fail(message,
             !authentication
                 && event.value(QStringLiteral("error")).toObject()
                        .value(QStringLiteral("retryable")).toBool(true),
             authentication ? QStringLiteral("authentication")
                            : (m_finalizing ? QStringLiteral("finalize")
                                            : QStringLiteral("streaming")));
        m_socket.close();
    }
#else
    Q_UNUSED(message)
#endif
}

void CodexDictationClient::fail(const QString &message,
                                bool retryable,
                                const QString &phase)
{
    if (m_cancelled || m_failureEmitted || m_sessionClosed) {
        return;
    }
    m_failureEmitted = true;
    emit failed(message, retryable, phase);
}

} // namespace speecher
