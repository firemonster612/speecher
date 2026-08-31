#include "providers/AnthropicApiRefiner.h"

#include "providers/ClaudeCredentials.h"
#include "providers/TranscriptRefinementPrompt.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QUuid>

namespace speecher {
namespace {

constexpr int absoluteDeadlineMs = 120000;

QString anthropicErrorMessage(const QByteArray &payload, const QString &fallback)
{
    const QJsonObject object = QJsonDocument::fromJson(payload).object();
    const QJsonObject error = object.value(QStringLiteral("error")).toObject();
    const QString message = error.value(QStringLiteral("message")).toString();
    const QString type = error.value(QStringLiteral("type")).toString();
    if (message.isEmpty()) {
        return fallback;
    }
    return type.isEmpty() ? message : QStringLiteral("%1: %2").arg(type, message);
}

QByteArray claudeCodeUserAgent()
{
    const QString version = ClaudeCredentials::installedVersion();
    const QString normalizedVersion = version.isEmpty() ? QStringLiteral("unknown") : version;
    return QStringLiteral("claude-cli/%1 (external, cli)").arg(normalizedVersion).toUtf8();
}

bool modelSupportsAdaptiveEffort(const QString &model)
{
    const QString normalized = model.toCaseFolded();
    return normalized.contains(QStringLiteral("sonnet-4-6"))
        || normalized.contains(QStringLiteral("opus-4-8"))
        || normalized.contains(QStringLiteral("opus-4-7"))
        || normalized.contains(QStringLiteral("opus-4-6"))
        || normalized.contains(QStringLiteral("opus-4-5"));
}

// Fast mode is a research preview limited to Opus 5 and Opus 4.8; sending
// speed=fast for other models would fail every request before the fallback.
bool modelSupportsFastMode(const QString &model)
{
    const QString normalized = model.toCaseFolded();
    return normalized.contains(QStringLiteral("opus-5"))
        || normalized.contains(QStringLiteral("opus-4-8"));
}

bool modelSupportsExtraHighEffort(const QString &model)
{
    const QString normalized = model.toCaseFolded();
    return normalized.contains(QStringLiteral("opus-4-8"))
        || normalized.contains(QStringLiteral("opus-4-7"));
}

QString apiEffortForModel(const QString &model, const QString &effort)
{
    if (effort == QStringLiteral("xhigh") && !modelSupportsExtraHighEffort(model)) {
        return QStringLiteral("max");
    }
    if (effort == QStringLiteral("low") || effort == QStringLiteral("medium")
        || effort == QStringLiteral("high") || effort == QStringLiteral("xhigh")
        || effort == QStringLiteral("max")) {
        return effort;
    }
    return QStringLiteral("high");
}

QString claudeCodeSystemPrompt(const QString &refinementStyle,
    const RefinementContext &context)
{
    return QStringLiteral("You are Claude Code, Anthropic's official CLI for Claude.\n\n")
        + (context.editSelection
               ? selectedDocumentEditingSystemPrompt(refinementStyle, context)
               : dictationRefinementSystemPrompt(refinementStyle, context));
}

} // namespace

AnthropicApiRefiner::AnthropicApiRefiner(QObject *parent, int requestTimeoutMs)
    : QObject(parent)
    , m_requestTimeoutMs(requestTimeoutMs)
{
    m_inactivityTimer.setSingleShot(true);
    m_deadlineTimer.setSingleShot(true);
    const auto failOnTimeout = [this] {
        if (!m_reply || m_failed || m_completed) {
            return;
        }
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->abort();
        if (retryWithoutFastMode(QStringLiteral("timed out"))) {
            return;
        }
        m_failed = true;
        emit failed(QStringLiteral("Anthropic refinement timed out waiting for a response"));
    };
    connect(&m_inactivityTimer, &QTimer::timeout, this, failOnTimeout);
    connect(&m_deadlineTimer, &QTimer::timeout, this, failOnTimeout);
}

void AnthropicApiRefiner::refine(const QString &rawTranscript,
                                 const QStringList &vocabulary,
                                 const QStringList &bindingVocabulary,
                                 const QString &bearerToken,
                                 const QString &endpointBase,
                                 const QString &model,
                                 const QString &effort,
                                 bool fastMode,
                                 const QString &refinementStyle,
                                 const RefinementContext &context)
{
    cancel();
    m_accumulated.clear();
    m_buffer.clear();
    m_failed = false;
    m_completed = false;
    const bool fast = fastMode && modelSupportsFastMode(model);
    m_fastModeFallback = fast
        ? [=, this] {
              refine(rawTranscript, vocabulary, bindingVocabulary, bearerToken, endpointBase,
                     model, effort, false, refinementStyle, context);
          }
        : std::function<void()>();

    QUrl endpoint(endpointBase.isEmpty() ? QStringLiteral("https://api.anthropic.com/v1") : endpointBase);
    endpoint.setPath(endpoint.path().replace(QRegularExpression(QStringLiteral("/$")), QString()) + QStringLiteral("/messages"));

    QNetworkRequest request(endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", "Bearer " + bearerToken.toUtf8());
    request.setRawHeader("anthropic-version", "2023-06-01");
    request.setRawHeader("anthropic-beta",
                         fast ? "claude-code-20250219,oauth-2025-04-20,fast-mode-2026-02-01"
                              : "claude-code-20250219,oauth-2025-04-20");
    request.setRawHeader("User-Agent", claudeCodeUserAgent());
    request.setRawHeader("x-app", "cli");
    const QByteArray requestId = QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8();
    request.setRawHeader("x-claude-code-session-id", requestId);
    request.setRawHeader("x-client-request-id", requestId);

    QJsonObject body;
    body.insert(QStringLiteral("model"), model);
    body.insert(QStringLiteral("max_tokens"), 4096);
    body.insert(QStringLiteral("stream"), true);
    if (fast) {
        body.insert(QStringLiteral("speed"), QStringLiteral("fast"));
    }
    if (modelSupportsAdaptiveEffort(model)) {
        body.insert(QStringLiteral("thinking"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("adaptive")},
            {QStringLiteral("display"), QStringLiteral("omitted")},
        });
        body.insert(QStringLiteral("output_config"), QJsonObject{
            {QStringLiteral("effort"), apiEffortForModel(model, effort)},
        });
    }
    qInfo().noquote() << "anthropic oauth refinement request model=" + model
                      << "effort=" + (body.value(QStringLiteral("output_config")).toObject().value(QStringLiteral("effort")).toString(QStringLiteral("default")))
                      << "endpoint=" + endpoint.toString(QUrl::RemoveUserInfo);
    body.insert(QStringLiteral("system"), claudeCodeSystemPrompt(refinementStyle, context));
    const QString userMessage = transcriptRefinementUserMessage(
        rawTranscript,
        vocabulary,
        bindingVocabulary,
        context);
    QJsonValue content = userMessage;
    if (context.hasScreenshot() && !context.editSelection) {
        content = QJsonArray{
            QJsonObject{
                {QStringLiteral("type"), QStringLiteral("text")},
                {QStringLiteral("text"), userMessage},
            },
            QJsonObject{
                {QStringLiteral("type"), QStringLiteral("image")},
                {QStringLiteral("source"),
                 QJsonObject{
                     {QStringLiteral("type"), QStringLiteral("base64")},
                     {QStringLiteral("media_type"), context.screenshotMediaType},
                     {QStringLiteral("data"), QString::fromLatin1(context.screenshotData.toBase64())},
                 }},
            },
        };
    }
    body.insert(QStringLiteral("messages"),
                QJsonArray{QJsonObject{
                    {QStringLiteral("role"), QStringLiteral("user")},
                    {QStringLiteral("content"), content},
                }});

    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_reply = reply;
    m_inactivityTimer.start(m_requestTimeoutMs);
    m_deadlineTimer.start(absoluteDeadlineMs);
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        if (reply != m_reply) {
            return;
        }
        parseSseChunk(reply->readAll());
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (reply != m_reply) {
            reply->deleteLater();
            return;
        }
        m_inactivityTimer.stop();
        m_deadlineTimer.stop();
        m_reply = nullptr;
        if (m_failed || m_completed) {
            reply->deleteLater();
            return;
        }
        QString message;
        if (reply->error() != QNetworkReply::NoError) {
            const QByteArray payload = m_buffer + reply->readAll();
            message = QStringLiteral("Anthropic refinement failed: %1")
                          .arg(anthropicErrorMessage(payload, reply->errorString()));
        } else if (m_accumulated.isEmpty()) {
            message = QStringLiteral("Anthropic refinement failed: empty response");
        } else {
            message = QStringLiteral("Anthropic refinement failed: stream ended before completion");
        }
        reply->deleteLater();
        if (retryWithoutFastMode(message)) {
            return;
        }
        emit failed(message);
    });
}

void AnthropicApiRefiner::cancel()
{
    m_fastModeFallback = nullptr;
    m_inactivityTimer.stop();
    m_deadlineTimer.stop();
    if (m_reply) {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->abort();
    }
}

void AnthropicApiRefiner::parseSseChunk(const QByteArray &chunk)
{
    m_buffer += chunk;
    while (true) {
        int boundary = m_buffer.indexOf("\n\n");
        int separatorBytes = 2;
        const int crlfBoundary = m_buffer.indexOf("\r\n\r\n");
        if (crlfBoundary >= 0 && (boundary < 0 || crlfBoundary < boundary)) {
            boundary = crlfBoundary;
            separatorBytes = 4;
        }
        if (boundary < 0) {
            break;
        }
        const QByteArray frame = m_buffer.left(boundary);
        m_buffer.remove(0, boundary + separatorBytes);

        QByteArray eventName;
        QByteArray data;
        for (const QByteArray &line : frame.split('\n')) {
            if (line.startsWith("event:")) {
                eventName = line.mid(6).trimmed();
            } else if (line.startsWith("data:")) {
                data += line.mid(5).trimmed();
            }
        }
        const QJsonObject object = QJsonDocument::fromJson(data).object();
        if (eventName == "error" || object.value(QStringLiteral("type")).toString() == QStringLiteral("error")) {
            m_inactivityTimer.stop();
            m_deadlineTimer.stop();
            QPointer<QNetworkReply> reply = m_reply;
            m_reply = nullptr;
            const QString message = anthropicErrorMessage(data, QStringLiteral("Anthropic refinement error"));
            if (!retryWithoutFastMode(message)) {
                m_failed = true;
                emit failed(message);
            }
            if (reply) {
                QMetaObject::invokeMethod(reply, &QNetworkReply::abort, Qt::QueuedConnection);
            }
            return;
        }
        if (eventName == "message_start"
            || eventName == "content_block_start"
            || eventName == "content_block_delta"
            || eventName == "content_block_stop"
            || eventName == "message_delta") {
            m_inactivityTimer.start(m_requestTimeoutMs);
        }
        if (eventName == "content_block_delta") {
            const QJsonObject deltaObject = object.value(QStringLiteral("delta")).toObject();
            if (deltaObject.value(QStringLiteral("type")).toString() == QStringLiteral("text_delta")) {
                const QString text = deltaObject.value(QStringLiteral("text")).toString();
                m_accumulated += text;
                emit delta(text);
            }
        } else if (eventName == "message_stop") {
            completeIfReady();
            return;
        }
    }
}

bool AnthropicApiRefiner::retryWithoutFastMode(const QString &reason)
{
    // Only retry when no deltas were emitted; a retry after streamed output
    // would replay the transcript into the live preview.
    if (!m_fastModeFallback || !m_accumulated.isEmpty()) {
        return false;
    }
    const std::function<void()> fallback = std::move(m_fastModeFallback);
    m_fastModeFallback = nullptr;
    qWarning().noquote() << "anthropic fast mode refinement failed, retrying at standard speed:" << reason;
    fallback();
    return true;
}

void AnthropicApiRefiner::completeIfReady()
{
    if (m_failed || m_completed || m_accumulated.isEmpty()) {
        return;
    }
    m_inactivityTimer.stop();
    m_deadlineTimer.stop();
    m_completed = true;
    QPointer<QNetworkReply> reply = m_reply;
    m_reply = nullptr;
    const QString result = m_accumulated;
    QMetaObject::invokeMethod(this, [this, reply, result] {
        if (reply) {
            reply->abort();
        }
        emit completed(result);
    }, Qt::QueuedConnection);
}

} // namespace speecher
