#include "providers/ClaudeVoiceClient.h"

#include "providers/ClaudeCredentials.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QDebug>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QUrlQuery>

#include <utility>

namespace speecher {

namespace {
constexpr qsizetype kMaxPendingAudioBytes = 15 * 16000 * 2;
}

static QString firstTranscriptText(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString();
    }
    if (value.isArray()) {
        for (const QJsonValue &item : value.toArray()) {
            const QString text = firstTranscriptText(item);
            if (!text.isEmpty()) {
                return text;
            }
        }
    }
    if (!value.isObject()) {
        return {};
    }

    const QJsonObject object = value.toObject();
    for (const QString &key : {QStringLiteral("text"),
                               QStringLiteral("transcript"),
                               QStringLiteral("data"),
                               QStringLiteral("delta"),
                               QStringLiteral("partial"),
                               QStringLiteral("final"),
                               QStringLiteral("content"),
                               QStringLiteral("value")}) {
        const QString text = firstTranscriptText(object.value(key));
        if (!text.isEmpty()) {
            return text;
        }
    }
    for (const QJsonValue &child : object) {
        if (child.isObject() || child.isArray()) {
            const QString text = firstTranscriptText(child);
            if (!text.isEmpty()) {
                return text;
            }
        }
    }
    return {};
}

static bool messageLooksFinal(const QJsonObject &object)
{
    const QString combined = QStringList{
        object.value(QStringLiteral("type")).toString(),
        object.value(QStringLiteral("event")).toString(),
        object.value(QStringLiteral("message")).toString(),
    }.join(' ').toLower();
    return combined.contains(QStringLiteral("final"))
        || combined.contains(QStringLiteral("complete"))
        || object.value(QStringLiteral("is_final")).toBool()
        || object.value(QStringLiteral("final")).isBool() && object.value(QStringLiteral("final")).toBool();
}

static QString redactedErrorSummary(const QJsonObject &object)
{
    const QJsonValue errorValue = object.value(QStringLiteral("error"));
    QJsonObject error = errorValue.toObject();
    if (error.isEmpty() && errorValue.isString()) {
        return errorValue.toString().left(240);
    }
    QStringList parts;
    for (const QString &key : {QStringLiteral("type"), QStringLiteral("code"), QStringLiteral("error_code"), QStringLiteral("message"), QStringLiteral("description")}) {
        const QString value = error.value(key).toString(object.value(key).toString());
        if (!value.isEmpty()) {
            parts << key + QStringLiteral("=") + value.left(240);
        }
    }
    return parts.join(QStringLiteral(" "));
}

static bool envFlag(const char *name, bool defaultValue)
{
    const QString value = qEnvironmentVariable(name).trimmed().toLower();
    if (value.isEmpty()) {
        return defaultValue;
    }
    if (value == QStringLiteral("1") || value == QStringLiteral("true") || value == QStringLiteral("yes") || value == QStringLiteral("on")) {
        return true;
    }
    if (value == QStringLiteral("0") || value == QStringLiteral("false") || value == QStringLiteral("no") || value == QStringLiteral("off")) {
        return false;
    }
    return defaultValue;
}

static bool typedInterimsEnabled()
{
    if (envFlag("CLAUDE_CODE_VOICE_FORWARD_INTERIMS_TYPED", false)) {
        return true;
    }
    return envFlag("SPEECHER_CLAUDE_FORWARD_INTERIMS_TYPED", true);
}

QUrlQuery claudeVoiceStreamQuery(const QStringList &vocabulary)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("encoding"), QStringLiteral("linear16"));
    query.addQueryItem(QStringLiteral("sample_rate"), QStringLiteral("16000"));
    query.addQueryItem(QStringLiteral("channels"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("endpointing_ms"), QStringLiteral("300"));
    query.addQueryItem(QStringLiteral("utterance_end_ms"), QStringLiteral("1000"));
    query.addQueryItem(QStringLiteral("language"), QStringLiteral("en"));
    query.addQueryItem(QStringLiteral("use_conversation_engine"), QStringLiteral("true"));
    if (typedInterimsEnabled()) {
        query.addQueryItem(QStringLiteral("forward_interims"), QStringLiteral("typed"));
    }
    query.addQueryItem(QStringLiteral("stt_provider"), QStringLiteral("deepgram-nova3"));
    for (const QString &term : vocabulary) {
        query.addQueryItem(QStringLiteral("keyterms"), term);
    }
    return query;
}

ClaudeVoiceClient::ClaudeVoiceClient(QObject *parent)
    : QObject(parent)
    , m_debugSchema(qEnvironmentVariable("SPEECHER_DEBUG_CLAUDE_SCHEMA") == QStringLiteral("1"))
{
#ifdef SPEECHER_WITH_QT_WEBSOCKETS
    m_keepAliveTimer.setInterval(8000);
    connect(&m_keepAliveTimer, &QTimer::timeout, this, [this] {
        if (m_socket.state() == QAbstractSocket::ConnectedState) {
            qInfo() << "claude keepalive sent";
            m_socket.sendTextMessage(QStringLiteral("{\"type\":\"KeepAlive\"}"));
        }
    });
    connect(&m_socket, &QWebSocket::connected, this, [this] {
        m_connected = true;
        qInfo() << "claude websocket connected";
        if (m_finalizing) {
            qInfo() << "claude websocket connected after stop; closing";
            m_socket.close();
            return;
        }
        qInfo() << "claude initial keepalive sent";
        m_socket.sendTextMessage(QStringLiteral("{\"type\":\"KeepAlive\"}"));
        flushPendingAudio();
        m_keepAliveTimer.start();
        emit connected();
    });
    connect(&m_socket, &QWebSocket::disconnected, this, [this] {
        m_connected = false;
        m_finalizing = false;
        m_keepAliveTimer.stop();
        qInfo() << "claude websocket disconnected";
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
        emit failed(QStringLiteral("Claude voice API changed or connection failed: %1").arg(m_socket.errorString()));
    });
#endif
}

void ClaudeVoiceClient::start(const QUrl &url, const QString &accessToken, const QStringList &vocabulary)
{
#ifdef SPEECHER_WITH_QT_WEBSOCKETS
    QUrl streamUrl(url);
    streamUrl.setQuery(claudeVoiceStreamQuery(vocabulary));

    m_lastInterim.clear();
    m_finalizing = false;
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
    qInfo().noquote() << "claude websocket opening url=" + streamUrl.toString(QUrl::RemoveUserInfo)
                      << "vocabularyCount=" + QString::number(vocabulary.size());
    m_socket.open(request);
#else
    Q_UNUSED(url)
    Q_UNUSED(accessToken)
    Q_UNUSED(vocabulary)
    emit failed(QStringLiteral("Qt WebSockets support was not built; install Qt6 WebSockets development files and rebuild"));
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
    if (m_connected && !m_finalizing) {
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
    clearPendingAudio();
    if (m_connected) {
        m_finalizing = true;
        const quint64 sessionId = m_sessionId;
        m_socket.sendTextMessage(QStringLiteral("{\"type\":\"CloseStream\"}"));
        QTimer::singleShot(5000, this, [this, sessionId] {
            if (sessionId == m_sessionId && m_finalizing && m_socket.state() == QAbstractSocket::ConnectedState) {
                qWarning() << "claude finalize timeout closing websocket";
                m_socket.close();
            }
        });
    } else {
        m_finalizing = true;
        if (m_socket.state() != QAbstractSocket::UnconnectedState) {
            m_socket.close();
        }
    }
    qInfo() << "claude close requested connected=" << m_connected;
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

    m_pendingAudio.append(pcm);
    m_pendingAudioBytes += pcm.size();
    while (m_pendingAudioBytes > kMaxPendingAudioBytes && !m_pendingAudio.isEmpty()) {
        m_pendingAudioBytes -= m_pendingAudio.takeFirst().size();
    }
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
    const QString type = object.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("error") || object.contains(QStringLiteral("error"))) {
        qWarning().noquote() << "claude server error " + redactedErrorSummary(object);
        emit failed(QStringLiteral("Claude voice server error: %1").arg(redactedErrorSummary(object)));
        return;
    }
    if (type == QStringLiteral("TranscriptEndpoint")) {
        qInfo() << "claude transcript endpoint lastInterimLength=" << m_lastInterim.size();
        if (!m_lastInterim.isEmpty()) {
            emit finalTranscript(m_lastInterim);
            m_lastInterim.clear();
        }
        if (m_finalizing) {
            m_socket.close();
        }
        return;
    }
    if (type == QStringLiteral("TranscriptError")) {
        const QString summary = redactedErrorSummary(object);
        qWarning().noquote() << "claude transcript error " + summary;
        emit failed(summary.isEmpty() ? QStringLiteral("Claude transcript error") : summary);
        return;
    }
    const QString text = firstTranscriptText(object).simplified();
    if (text.isEmpty()) {
        qInfo() << "claude text message contained no transcript text";
        return;
    }
    if (messageLooksFinal(object)) {
        qInfo() << "claude final transcript length=" << text.size();
        m_lastInterim.clear();
        emit finalTranscript(text);
    } else {
        qInfo() << "claude partial transcript length=" << text.size();
        m_lastInterim = text;
        emit partialTranscript(text);
    }
#else
    Q_UNUSED(message)
#endif
}

} // namespace speecher
