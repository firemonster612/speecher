#include "providers/OpenAiRefiner.h"
#include "providers/EndpointRequest.h"
#include "providers/ProviderFailureClassification.h"

#include "core/AppSettings.h"
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
        const QJsonObject source = terminalFailure ? response : object;
        const QJsonValue error = source.value(QStringLiteral("error"));
        // An incomplete response hit a token limit or a filter: the text is cut short.
        const ProviderFailureKind kind = name == "response.incomplete"
            ? ProviderFailureKind::InvalidResult
            : streamedErrorKind(error.isObject() ? error.toObject() : source);
        return {terminalFailure ? Event::Failed : Event::Rejected,
                openAiErrorMessage(terminalFailure ? QJsonDocument(response).toJson() : data, fallback), kind};
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

bool isPublicOpenAiApi(const QUrl &endpoint)
{
    return endpoint.host().compare(QStringLiteral("api.openai.com"), Qt::CaseInsensitive) == 0;
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
                           const QString &speed,
                           const QString &refinementStyle,
                           const RefinementContext &context)
{
    // Fast mode is the ChatGPT backend's priority tier, and Ultrafast its
    // ultrafast tier, reached directly with a ChatGPT account or through a CLI
    // Proxy API server (which maps "fast" to "priority" itself). The public API
    // bills both at a higher per-token rate, so API-key requests to
    // api.openai.com never ask for either.
    const QUrl base(endpointBase.isEmpty() ? QStringLiteral("https://api.openai.com/v1") : endpointBase);
    const bool publicApi = isPublicOpenAiApi(base);
    // GPT-6.1 Sol refuses "none"; low is the least effort it takes.
    const QString requestedEffort = effort.isEmpty() ? QStringLiteral("none") : effort;
    const QString sentEffort =
        requestedEffort == QStringLiteral("none")
                && model.trimmed().toCaseFolded().startsWith(QStringLiteral("gpt-6.1-sol"))
            ? QStringLiteral("low")
            : requestedEffort;
    const QString effectiveSpeed = effectiveOpenAiSpeed(speed, model);
    // chatgpt.com rejects "fast" (HTTP 400 "Unsupported service_tier: fast"); "priority" is accepted.
    const QString serviceTier = effectiveSpeed == QStringLiteral("ultrafast") ? QStringLiteral("ultrafast")
        : effectiveSpeed == QStringLiteral("fast")                            ? QStringLiteral("priority")
                                                                              : QString();
    m_stream.start([=](bool fast) -> StreamingRefinement::Request {
        QUrl endpoint = base;
        endpoint.setPath(endpoint.path().replace(QRegularExpression(QStringLiteral("/$")), QString()) + QStringLiteral("/responses"));

        QNetworkRequest request = endpointRequest(endpoint);
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
        body.insert(QStringLiteral("reasoning"), QJsonObject{{QStringLiteral("effort"), sentEffort}});
        body.insert(QStringLiteral("instructions"), refinementSystemPrompt(refinementStyle, context));
        body.insert(QStringLiteral("stream"), true);
        body.insert(QStringLiteral("store"), false);
        if (fast) {
            body.insert(QStringLiteral("service_tier"), serviceTier);
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
    }, publicApi ? QString() : serviceTier);
}

void OpenAiRefiner::cancel()
{
    m_stream.cancel();
}

} // namespace speecher
