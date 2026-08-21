#include "providers/OpenAiRefiner.h"

#include "providers/TranscriptRefinementPrompt.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
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

} // namespace

OpenAiRefiner::OpenAiRefiner(QObject *parent, int requestTimeoutMs)
    : QObject(parent)
    , m_requestTimeoutMs(requestTimeoutMs)
{
    m_deadlineTimer.setSingleShot(true);
    connect(&m_deadlineTimer, &QTimer::timeout, this, [this] {
        if (!m_reply || m_failed || m_completed) {
            return;
        }
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        m_failed = true;
        reply->abort();
        emit failed(QStringLiteral("OpenAI refinement exceeded its response deadline"));
    });
}

void OpenAiRefiner::refine(const QString &rawTranscript,
                           const QStringList &vocabulary,
                           const QStringList &bindingVocabulary,
                           const QString &bearerToken,
                           const QString &organization,
                           const QString &project,
                           const QString &endpointBase,
                           const QString &accountId,
                           bool chatgptBackend,
                           const QString &model,
                           const QString &effort,
                           const QString &refinementStyle,
                           const RefinementContext &context)
{
    Q_UNUSED(chatgptBackend)
    cancel();
    m_accumulated.clear();
    m_buffer.clear();
    m_failed = false;
    m_completed = false;

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

    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_reply = reply;
    m_deadlineTimer.start(m_requestTimeoutMs);
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
        m_deadlineTimer.stop();
        m_reply = nullptr;
        if (m_failed || m_completed) {
            reply->deleteLater();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            const QByteArray payload = m_buffer + reply->readAll();
            emit failed(QStringLiteral("OpenAI refinement failed: %1").arg(openAiErrorMessage(payload, reply->errorString())));
        } else if (!m_accumulated.isEmpty()) {
            completeIfReady();
        } else if (!m_failed) {
            emit failed(QStringLiteral("OpenAI refinement failed: empty response"));
        }
        reply->deleteLater();
    });
}

void OpenAiRefiner::cancel()
{
    m_deadlineTimer.stop();
    if (m_reply) {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->abort();
    }
}

void OpenAiRefiner::parseSseChunk(const QByteArray &chunk)
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
        if (eventName == "error") {
            m_deadlineTimer.stop();
            m_failed = true;
            if (m_reply) {
                QNetworkReply *reply = m_reply;
                m_reply = nullptr;
                reply->abort();
            }
            emit failed(openAiErrorMessage(data, QStringLiteral("OpenAI refinement error")));
            return;
        }
        const QJsonObject object = QJsonDocument::fromJson(data).object();
        if (eventName == "response.output_text.delta") {
            const QString text = object.value(QStringLiteral("delta")).toString();
            m_accumulated += text;
            emit delta(text);
        } else if (eventName == "response.completed") {
            completeIfReady();
        }
    }
}

void OpenAiRefiner::completeIfReady()
{
    if (m_failed || m_completed || m_accumulated.isEmpty()) {
        return;
    }
    m_deadlineTimer.stop();
    m_completed = true;
    emit completed(m_accumulated);
}

} // namespace speecher
