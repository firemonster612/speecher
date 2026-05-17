#include "providers/OpenAiRefiner.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

namespace speecher {

OpenAiRefiner::OpenAiRefiner(QObject *parent)
    : QObject(parent)
{
}

static QString refinementInstructions(const QString &style, const QString &format)
{
    QStringList parts{
        QStringLiteral("You refine dictated speech-to-text into clean text.\n"
                       "Preserve the user's intent and factual meaning.\n"
                       "Do not add new facts, claims, or ideas.\n"
                       "Return only the refined text.\n"
                       "Do not include commentary, explanations, labels, or quotes around the answer.\n"
                       "Apply spoken corrections inside the current transcript before producing the final text.\n"
                       "Remove correction phrases after applying them."),
        QStringLiteral("Preserve commands, file paths, URLs, environment variables, identifiers, package names, function names, error messages, and quoted code-like text mostly literally.\n"
                       "Convert spoken technical symbols into literal characters when context makes that likely: slash, backslash, dash, hyphen, underscore, dot, colon, pipe, equals, and at.\n"
                       "Use backticks sparingly: only wrap exact commands, file paths, URLs, environment variables, inline code, and verbatim error strings.\n"
                       "Do not wrap ordinary technical terms, app names, package names, product names, feature names, UI labels, or natural-language phrases in backticks."),
    };

    if (style == QStringLiteral("strong_polish")) {
        parts << QStringLiteral("Use strong polish.\n"
                                "Aggressively clean up speech artifacts, filler words, false starts, duplicated words, and awkward phrasing.\n"
                                "Improve clarity, flow, grammar, and organization while preserving meaning.\n"
                                "Infer useful structure when the transcript suggests lists, ordered steps, sections, or separate paragraphs.\n"
                                "Handle broad natural corrections such as \"actually\", \"wait\", \"let me rephrase\", \"what I meant was\", \"I meant X not Y\", \"replace X with Y\", and \"change the second item to\".\n"
                                "For \"oops remove that\" or \"scratch that\", remove the last coherent thought: the most recent phrase, clause, sentence, or list item based on context.");
    } else if (style == QStringLiteral("light_cleanup")) {
        parts << QStringLiteral("Use light cleanup.\n"
                                "Stay close to the original transcript.\n"
                                "Fix punctuation, capitalization, spacing, and obvious speech-to-text mistakes.\n"
                                "Avoid restructuring, reordering, or rewriting unless the user explicitly dictated formatting.\n"
                                "Handle only explicit corrections such as \"scratch that\", \"remove that\", \"replace X with Y\", and \"change X to Y\".\n"
                                "For \"remove that\" or \"scratch that\", remove the last coherent thought only when the target is clear.");
    } else {
        parts << QStringLiteral("Use balanced cleanup.\n"
                                "Fix punctuation, capitalization, grammar, obvious transcription errors, repeated words, and spacing.\n"
                                "Lightly improve wording when the intended meaning is clear.\n"
                                "Infer simple structure only when the transcript clearly suggests it.\n"
                                "Avoid heavy rewriting.\n"
                                "Handle common corrections such as \"oops remove that\", \"scratch that\", \"never mind\", \"I mean\", \"I meant X not Y\", \"X not Y\", and \"replace X with Y\".\n"
                                "For \"oops remove that\" or \"scratch that\", remove the last coherent thought.");
    }

    if (format == QStringLiteral("markdown")) {
        parts << QStringLiteral("Use Markdown-style plain text when useful.\n"
                                "Use short headings, hyphen bullets, numbered lists, and blank lines when they improve readability.\n"
                                "Convert clear spoken structure cues such as \"bullet list\", \"numbered list\", \"first\", \"second\", \"next section\", \"heading\", \"new paragraph\", and \"new line\" into appropriate formatting.");
    } else {
        parts << QStringLiteral("Use plain sentence output.\n"
                                "Prefer compact paragraphs and sentence-style lists.\n"
                                "Do not infer Markdown headings, bullet lists, or vertical numbered lists.\n"
                                "Honor explicit paragraph and line-break cues such as \"new paragraph\" and \"new line\".\n"
                                "If the user gives list-like content, format it as a sentence list using words such as first, second, and third, or semicolons.");
    }

    return parts.join(QStringLiteral("\n\n"));
}

static QString openAiErrorMessage(const QByteArray &payload, const QString &fallback)
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

void OpenAiRefiner::refine(const QString &rawTranscript,
                           const QStringList &vocabulary,
                           const QString &bearerToken,
                           const QString &organization,
                           const QString &project,
                           const QString &endpointBase,
                           const QString &accountId,
                           bool chatgptBackend,
                           const QString &model,
                           const QString &refinementStyle,
                           const QString &outputFormat)
{
    Q_UNUSED(chatgptBackend)
    cancel();
    m_accumulated.clear();
    m_buffer.clear();
    m_failed = false;

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
    body.insert(QStringLiteral("reasoning"), QJsonObject{{QStringLiteral("effort"), QStringLiteral("none")}});
    body.insert(QStringLiteral("instructions"), refinementInstructions(refinementStyle, outputFormat));
    body.insert(QStringLiteral("stream"), true);
    body.insert(QStringLiteral("store"), false);
    QJsonObject user;
    user.insert(QStringLiteral("role"), QStringLiteral("user"));
    user.insert(QStringLiteral("content"), QStringLiteral("Raw transcript:\n%1\n\nPreferred vocabulary:\n%2").arg(rawTranscript, vocabulary.join(QStringLiteral(", "))));
    body.insert(QStringLiteral("input"), QJsonArray{user});

    m_reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::readyRead, this, [this] { parseSseChunk(m_reply->readAll()); });
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        const auto reply = m_reply;
        m_reply = nullptr;
        if (reply->error() != QNetworkReply::NoError && m_accumulated.isEmpty()) {
            const QByteArray payload = m_buffer + reply->readAll();
            emit failed(QStringLiteral("OpenAI refinement failed: %1")
                            .arg(openAiErrorMessage(payload, reply->errorString())));
        } else if (!m_accumulated.isEmpty()) {
            emit completed(m_accumulated);
        } else if (!m_failed) {
            emit failed(QStringLiteral("OpenAI refinement failed: empty response"));
        }
        reply->deleteLater();
    });
}

void OpenAiRefiner::cancel()
{
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

void OpenAiRefiner::parseSseChunk(const QByteArray &chunk)
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
        if (eventName == "error") {
            m_failed = true;
            emit failed(openAiErrorMessage(data, QStringLiteral("OpenAI refinement error")));
            continue;
        }
        const QJsonObject object = QJsonDocument::fromJson(data).object();
        if (eventName == "response.output_text.delta") {
            const QString text = object.value(QStringLiteral("delta")).toString();
            m_accumulated += text;
            emit delta(text);
        } else if (eventName == "response.completed") {
            emit completed(m_accumulated);
        }
    }
}

} // namespace speecher
