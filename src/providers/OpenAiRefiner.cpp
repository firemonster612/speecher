#include "providers/OpenAiRefiner.h"

#include "providers/TranscriptRefinementPrompt.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRegularExpression>

namespace speecher {

namespace {

QString openAiErrorMessage(const QByteArray &payload, const QString &fallback)
{
    const QJsonObject object = QJsonDocument::fromJson(payload).object();
    const QJsonObject error = object.value(QStringLiteral("error")).toObject();
    const QString message = error.value(QStringLiteral("message")).toString();
    const QString code = error.value(QStringLiteral("code")).toString();
    if (message.isEmpty()) {
        return fallback;
    }
    return code.isEmpty() ? message : QStringLiteral("%1: %2").arg(code, message);
}

StreamingRefinement::Event openAiEvent(const QByteArray &name, const QByteArray &data)
{
    using Event = StreamingRefinement::Event;
    const QJsonObject object = QJsonDocument::fromJson(data).object();
    const bool terminalFailure = name == "response.failed" || name == "response.incomplete";
    if (name == "error" || terminalFailure) {
        const QJsonObject response = object.value(QStringLiteral("response")).toObject();
        const QString reason = response.value(QStringLiteral("incomplete_details")).toObject()
                                   .value(QStringLiteral("reason")).toString();
        const QString fallback = reason.isEmpty()
            ? QStringLiteral("OpenAI refinement error: %1").arg(QString::fromLatin1(name)) : reason;
        return {terminalFailure ? Event::Failed : Event::Rejected,
                openAiErrorMessage(terminalFailure ? QJsonDocument(response).toJson() : data, fallback)};
    }
    if (name == "response.output_text.delta") {
        return {Event::Delta, object.value(QStringLiteral("delta")).toString()};
    }
    if (name == "response.completed") return {Event::Complete, {}};
    if (name.startsWith("response.")) return {Event::Progress, {}};
    return {};
}

} // namespace

OpenAiRefiner::OpenAiRefiner(QObject *parent,
                             int requestTimeoutMs,
                             int absoluteDeadlineMs)
    : QObject(parent)
    , m_stream(QStringLiteral("OpenAI"), openAiEvent, openAiErrorMessage,
               requestTimeoutMs, absoluteDeadlineMs, this)
{
    connect(&m_stream, &StreamingRefinement::delta, this, &OpenAiRefiner::delta);
    connect(&m_stream, &StreamingRefinement::completed, this, &OpenAiRefiner::completed);
    connect(&m_stream, &StreamingRefinement::failed, this, &OpenAiRefiner::failed);
}

void OpenAiRefiner::refine(const QString &rawTranscript,
                           const QStringList &vocabulary,
                           const QStringList &bindingVocabulary,
                           const QString &bearerToken,
                           const QString &organization,
                           const QString &project,
                           const QString &endpointBase,
                           const QString &accountId,
                           const QString &model,
                           const QString &effort,
                           bool fastMode,
                           const QString &refinementStyle,
                           const RefinementContext &context)
{
    m_stream.start([=](bool fast) -> StreamingRefinement::Request {
        QUrl endpoint(endpointBase.isEmpty() ? QStringLiteral("https://api.openai.com/v1") : endpointBase);
        endpoint.setPath(endpoint.path().replace(QRegularExpression(QStringLiteral("/$")), QString()) + QStringLiteral("/responses"));

        QNetworkRequest request(endpoint);
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        request.setRawHeader("Authorization", "Bearer " + bearerToken.toUtf8());
        if (!organization.isEmpty()) {
            request.setRawHeader("OpenAI-Organization", organization.toUtf8());
        }
        if (!project.isEmpty()) {
            request.setRawHeader("OpenAI-Project", project.toUtf8());
        }
        if (!accountId.isEmpty()) {
            request.setRawHeader("ChatGPT-Account-ID", accountId.toUtf8());
        }

        QJsonObject body;
        body.insert(QStringLiteral("model"), model);
        body.insert(QStringLiteral("reasoning"), QJsonObject{{QStringLiteral("effort"), effort.isEmpty() ? QStringLiteral("none") : effort}});
        body.insert(QStringLiteral("instructions"),
                    context.editSelection
                        ? selectedDocumentEditingSystemPrompt(refinementStyle, context)
                        : dictationRefinementSystemPrompt(refinementStyle, context));
        body.insert(QStringLiteral("stream"), true);
        body.insert(QStringLiteral("store"), false);
        if (fast) {
            body.insert(QStringLiteral("service_tier"), QStringLiteral("fast"));
        }
        QJsonObject user;
        user.insert(QStringLiteral("role"), QStringLiteral("user"));
        const QString userMessage = transcriptRefinementUserMessage(rawTranscript, vocabulary, bindingVocabulary, context);
        if (context.hasScreenshot() && !context.editSelection) {
            const QString imageUrl = QStringLiteral("data:%1;base64,%2").arg(context.screenshotMediaType, QString::fromLatin1(context.screenshotData.toBase64()));
            user.insert(QStringLiteral("content"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("input_text")}, {QStringLiteral("text"), userMessage}}, QJsonObject{{QStringLiteral("type"), QStringLiteral("input_image")}, {QStringLiteral("image_url"), imageUrl}, {QStringLiteral("detail"), QStringLiteral("low")}}});
        } else {
            user.insert(QStringLiteral("content"), userMessage);
        }
        body.insert(QStringLiteral("input"), QJsonArray{user});
        return {request, QJsonDocument(body).toJson(QJsonDocument::Compact)};
    }, fastMode);
}

void OpenAiRefiner::cancel()
{
    m_stream.cancel();
}

} // namespace speecher
