#include "providers/ChatCompletionsRefiner.h"

#include "providers/TranscriptRefinementPrompt.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>

namespace speecher {
namespace {

constexpr int maxRawErrorChars = 300;

// Servers disagree on the error shape: OpenAI and llama-server nest an
// object under "error", Ollama's native API and some proxies use a string,
// older vLLM puts "message" at the top level, and FastAPI validation errors
// carry a "detail" array. The raw body is the last resort because it names
// the rejected field more often than a transport error does.
QString chatCompletionsErrorMessage(const QByteArray &payload, const QString &fallback)
{
    const QJsonObject object = QJsonDocument::fromJson(payload).object();
    const QJsonValue error = object.value(QStringLiteral("error"));
    const QString message = error.isObject()
        ? error.toObject().value(QStringLiteral("message")).toString()
        : error.isString() ? error.toString() : object.value(QStringLiteral("message")).toString();
    if (!message.isEmpty()) {
        return message;
    }
    const QString raw = QString::fromUtf8(payload).trimmed();
    return raw.isEmpty() ? fallback : raw.left(maxRawErrorChars);
}

StreamingRefinement::Event chatCompletionsEvent(const QByteArray &, const QByteArray &data)
{
    using Event = StreamingRefinement::Event;
    if (data == "[DONE]") return {Event::Complete, {}};
    const QJsonObject object = QJsonDocument::fromJson(data).object();
    if (object.contains(QStringLiteral("error"))) {
        return {Event::Failed, chatCompletionsErrorMessage(data, QStringLiteral("stream error"))};
    }
    const QJsonObject choice = object.value(QStringLiteral("choices")).toArray().at(0).toObject();
    // Anything but a natural stop cut the text short.
    const QString finishReason = choice.value(QStringLiteral("finish_reason")).toString();
    if (!finishReason.isEmpty() && finishReason != QStringLiteral("stop")) {
        return {Event::Failed, QStringLiteral("the model stopped early: %1").arg(finishReason)};
    }
    const QString content = choice.value(QStringLiteral("delta")).toObject()
                                .value(QStringLiteral("content")).toString();
    if (!content.isEmpty()) return {Event::Delta, content};
    // Role headers, reasoning deltas and the finish chunk: the server is
    // working, and [DONE] follows.
    return {Event::Progress, {}};
}

bool namesReasoningField(const QString &message)
{
    return message.contains(QStringLiteral("reasoning_effort"))
        || message.contains(QStringLiteral("chat_template_kwargs"))
        || message.contains(QStringLiteral("enable_thinking"));
}

} // namespace

ChatCompletionsRefiner::ChatCompletionsRefiner(const QString &label,
                                               Audience audience,
                                               QObject *parent,
                                               int requestTimeoutMs,
                                               int absoluteDeadlineMs)
    : QObject(parent)
    , m_audience(audience)
    , m_stream(label, chatCompletionsEvent, chatCompletionsErrorMessage,
               requestTimeoutMs, absoluteDeadlineMs, this)
{
    connect(&m_stream, &StreamingRefinement::delta, this, [this](const QString &text) {
        m_streamedOutput = true;
        emit delta(text);
    });
    connect(&m_stream, &StreamingRefinement::completed, this, &ChatCompletionsRefiner::completed);
    connect(&m_stream, &StreamingRefinement::failed, this, [this](const QString &message) {
        // Text already delivered must never be replayed.
        if (m_sentReasoningFields && !m_streamedOutput && namesReasoningField(message)) {
            qInfo().noquote() << "chat completions server rejected the reasoning fields,"
                              << "retrying without them:" << message;
            m_rejectsReasoningFields.insert(m_serverModel);
            post(false);
            return;
        }
        emit failed(message);
    });
}

void ChatCompletionsRefiner::refine(const QString &rawTranscript,
                                    const QStringList &vocabulary,
                                    const QStringList &bindingVocabulary,
                                    const QString &apiKey,
                                    const QString &endpointBase,
                                    const QString &model,
                                    const QString &refinementStyle,
                                    const RefinementContext &context)
{
    m_serverModel = endpointBase + QLatin1Char('\n') + model;
    const bool smallModel = m_audience == Audience::SmallLocalModel;
    const QString systemPrompt = smallModel ? compactRefinementSystemPrompt(refinementStyle, context)
                                            : refinementSystemPrompt(refinementStyle, context);
    m_buildRequest = [=](bool withReasoningFields) -> StreamingRefinement::Request {
        QString base = endpointBase;
        while (base.endsWith(QLatin1Char('/'))) base.chop(1);
        QNetworkRequest request(QUrl(base + QStringLiteral("/chat/completions")));
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        if (!apiKey.isEmpty()) {
            request.setRawHeader("Authorization", "Bearer " + apiKey.toUtf8());
        }
        QJsonObject body{
            {QStringLiteral("model"), model},
            {QStringLiteral("stream"), true},
            {QStringLiteral("messages"), QJsonArray{
                QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                            {QStringLiteral("content"), systemPrompt}},
                QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                            {QStringLiteral("content"), transcriptRefinementUserMessage(
                                 rawTranscript, vocabulary, bindingVocabulary, context)}},
            }},
        };
        if (smallModel) {
            body.insert(QStringLiteral("temperature"), 0);
        }
        // Cleanup gains nothing from a reasoning pass and waits for all of
        // it. reasoning_effort covers Ollama, vLLM, LocalAI and KoboldCpp;
        // the template flag covers llama-server and templates that ignore it.
        if (withReasoningFields) {
            body.insert(QStringLiteral("reasoning_effort"), QStringLiteral("none"));
            body.insert(QStringLiteral("chat_template_kwargs"),
                        QJsonObject{{QStringLiteral("enable_thinking"), false}});
        }
        return {request, QJsonDocument(body).toJson(QJsonDocument::Compact)};
    };
    post(!m_rejectsReasoningFields.contains(m_serverModel));
}

void ChatCompletionsRefiner::post(bool withReasoningFields)
{
    m_sentReasoningFields = withReasoningFields;
    m_streamedOutput = false;
    m_stream.start([build = m_buildRequest, withReasoningFields](bool) {
        return build(withReasoningFields);
    }, false);
}

void ChatCompletionsRefiner::cancel()
{
    m_stream.cancel();
}

} // namespace speecher
