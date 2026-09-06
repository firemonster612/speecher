#include "providers/AnthropicApiRefiner.h"

#include "providers/ClaudeCredentials.h"
#include "providers/TranscriptRefinementPrompt.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

StreamingRefinement::Event anthropicEvent(const QByteArray &name, const QByteArray &data)
{
    using Event = StreamingRefinement::Event;
    const QJsonObject object = QJsonDocument::fromJson(data).object();
    const QJsonObject delta = object.value(QStringLiteral("delta")).toObject();
    const QString stopReason = delta.value(QStringLiteral("stop_reason")).toString();
    if (name == "message_delta" && !stopReason.isEmpty()
        && stopReason != QStringLiteral("end_turn") && stopReason != QStringLiteral("stop_sequence")) {
        return {Event::Failed, QStringLiteral("Anthropic refinement stopped: %1").arg(stopReason)};
    }
    if (name == "error" || object.value(QStringLiteral("type")).toString() == QStringLiteral("error")) {
        return {Event::Rejected, anthropicErrorMessage(data, QStringLiteral("Anthropic refinement error"))};
    }
    if (name == "content_block_delta" && delta.value(QStringLiteral("type")).toString() == QStringLiteral("text_delta")) {
        return {Event::Delta, delta.value(QStringLiteral("text")).toString()};
    }
    if (name == "message_stop") return {Event::Complete, {}};
    if (name == "message_start" || name == "content_block_start" || name == "content_block_delta"
        || name == "content_block_stop" || name == "message_delta") {
        return {Event::Progress, {}};
    }
    return {};
}

} // namespace

AnthropicApiRefiner::AnthropicApiRefiner(QObject *parent,
                                         int requestTimeoutMs,
                                         int absoluteDeadlineMs)
    : QObject(parent)
    , m_stream(QStringLiteral("Anthropic"), anthropicEvent, anthropicErrorMessage,
               requestTimeoutMs, absoluteDeadlineMs, this)
{
    connect(&m_stream, &StreamingRefinement::delta, this, &AnthropicApiRefiner::delta);
    connect(&m_stream, &StreamingRefinement::completed, this, &AnthropicApiRefiner::completed);
    connect(&m_stream, &StreamingRefinement::failed, this, &AnthropicApiRefiner::failed);
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
    m_stream.start([=](bool fast) -> StreamingRefinement::Request {
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
        return {request, QJsonDocument(body).toJson(QJsonDocument::Compact)};
    }, fastMode && modelSupportsFastMode(model));
}

void AnthropicApiRefiner::cancel()
{
    m_stream.cancel();
}

} // namespace speecher
