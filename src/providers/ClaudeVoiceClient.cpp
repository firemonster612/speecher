#include "providers/ClaudeVoiceClient.h"

#include "providers/ClaudeCredentials.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QDebug>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QSet>
#include <QUrlQuery>

#include <utility>

namespace speecher {

namespace {
constexpr qsizetype kMaximumKeytermBytes = 1024;
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

ClaudeVoiceEvent parseClaudeVoiceEvent(const QString &message)
{
    const QJsonObject object = QJsonDocument::fromJson(message.toUtf8()).object();
    const QString type = object.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("TranscriptInterim")
        || type == QStringLiteral("TranscriptText")) {
        return {ClaudeVoiceEventKind::Working,
                object.value(QStringLiteral("data")).toString(),
                {}};
    }
    if (type == QStringLiteral("TranscriptEndpoint")) {
        return {ClaudeVoiceEventKind::Endpoint,
                object.value(QStringLiteral("data")).toString(),
                {}};
    }
    if (type == QStringLiteral("TranscriptError")) {
        return {ClaudeVoiceEventKind::TranscriptError, {}, redactedErrorSummary(object)};
    }
    if (type == QStringLiteral("error") || object.contains(QStringLiteral("error"))) {
        return {ClaudeVoiceEventKind::Error, {}, redactedErrorSummary(object)};
    }
    return {};
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
    Q_UNUSED(vocabulary)
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
    return query;
}

QByteArray claudeVoiceKeytermsHeader(const QStringList &vocabulary)
{
    QByteArray header;
    QSet<QByteArray> seen;
    for (const QString &value : vocabulary) {
        const QString simplified = value.simplified();
        QByteArray term;
        term.reserve(simplified.size());
        for (QChar character : simplified) {
            const ushort unicode = character.unicode();
            if (unicode >= 0x20 && unicode <= 0x7e) {
                term.append(char(unicode));
            }
        }
        term = term.trimmed();
        const QByteArray key = term.toLower();
        if (term.isEmpty() || seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        const qsizetype separatorBytes = header.isEmpty() ? 0 : 1;
        const qsizetype available = kMaximumKeytermBytes - header.size() - separatorBytes;
        if (available <= 0) {
            break;
        }
        if (!header.isEmpty()) {
            header.append(',');
        }
        header.append(term.left(available));
        if (header.size() >= kMaximumKeytermBytes) {
            break;
        }
    }
    return header;
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
        const bool incompleteFinalization = (m_finalizing || m_finishRequested)
            && !m_completed
            && !m_cancelled
            && !m_failureEmitted;
        m_connected = false;
        m_finalizing = false;
        m_keepAliveTimer.stop();
        qInfo() << "claude websocket disconnected";
        if (incompleteFinalization) {
            fail(QStringLiteral("Claude voice stream closed before final transcript completion"),
                 true,
                 QStringLiteral("finalize"));
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
