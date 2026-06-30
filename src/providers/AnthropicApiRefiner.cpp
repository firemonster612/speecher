#include "providers/AnthropicApiRefiner.h"

#include "providers/ClaudeCredentials.h"
#include "providers/TranscriptRefinementPrompt.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUuid>

namespace speecher {
namespace {

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

QString claudeCodeSystemPrompt(const QString &refinementStyle)
{
    return QStringLiteral("You are Claude Code, Anthropic's official CLI for Claude.\n\n")
        + transcriptRefinementInstructions(refinementStyle);
}

} // namespace

AnthropicApiRefiner::AnthropicApiRefiner(QObject *parent)
    : QObject(parent)
{
}

void AnthropicApiRefiner::refine(const QString &rawTranscript,
                                 const QStringList &vocabulary,
                                 const QStringList &bindingVocabulary,
                                 const QString &bearerToken,
                                 const QString &endpointBase,
                                 const QString &model,
                                 const QString &effort,
                                 const QString &refinementStyle)
{
    cancel();
    m_accumulated.clear();
    m_buffer.clear();
    m_failed = false;
    m_completed = false;

    QUrl endpoint(endpointBase.isEmpty() ? QStringLiteral("https://api.anthropic.com/v1") : endpointBase);
    endpoint.setPath(endpoint.path().replace(QRegularExpression(QStringLiteral("/$")), QString()) + QStringLiteral("/messages"));

    QNetworkRequest request(endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", "Bearer " + bearerToken.toUtf8());
    request.setRawHeader("anthropic-version", "2023-06-01");
    request.setRawHeader("anthropic-beta", "claude-code-20250219,oauth-2025-04-20");
    request.setRawHeader("User-Agent", claudeCodeUserAgent());
    request.setRawHeader("x-app", "cli");
    const QByteArray requestId = QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8();
    request.setRawHeader("x-claude-code-session-id", requestId);
    request.setRawHeader("x-client-request-id", requestId);

    QJsonObject body;
    body.insert(QStringLiteral("model"), model);
    body.insert(QStringLiteral("max_tokens"), 4096);
    body.insert(QStringLiteral("stream"), true);
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
    body.insert(QStringLiteral("system"), claudeCodeSystemPrompt(refinementStyle));
    body.insert(QStringLiteral("messages"),
                QJsonArray{QJsonObject{
                    {QStringLiteral("role"), QStringLiteral("user")},
                    {QStringLiteral("content"), transcriptRefinementUserMessage(rawTranscript, vocabulary, bindingVocabulary)},
                }});

    m_reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::readyRead, this, [this] { parseSseChunk(m_reply->readAll()); });
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        const auto reply = m_reply;
        m_reply = nullptr;
        if (m_failed || m_completed) {
            reply->deleteLater();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            const QByteArray payload = m_buffer + reply->readAll();
            emit failed(QStringLiteral("Anthropic refinement failed: %1")
                            .arg(anthropicErrorMessage(payload, reply->errorString())));
        } else if (!m_accumulated.isEmpty()) {
            completeIfReady();
        } else if (!m_failed) {
            emit failed(QStringLiteral("Anthropic refinement failed: empty response"));
        }
        reply->deleteLater();
    });
}

void AnthropicApiRefiner::cancel()
{
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

void AnthropicApiRefiner::parseSseChunk(const QByteArray &chunk)
{
    m_buffer += chunk;
    while (true) {
        const int boundary = m_buffer.indexOf("\n\n");
        if (boundary < 0) {
            break;
        }
        const QByteArray frame = m_buffer.left(boundary);
        m_buffer.remove(0, boundary + 2);

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
            m_failed = true;
            emit failed(anthropicErrorMessage(data, QStringLiteral("Anthropic refinement error")));
            continue;
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
        }
    }
}

void AnthropicApiRefiner::completeIfReady()
{
    if (m_completed || m_accumulated.isEmpty()) {
        return;
    }
    m_completed = true;
    emit completed(m_accumulated);
}

} // namespace speecher
