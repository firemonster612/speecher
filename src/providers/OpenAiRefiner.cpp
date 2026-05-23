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

static QStringList alwaysRules()
{
    return {
        QStringLiteral("Rule: return_only_refined_text.\n"
                       "Return only the refined text. Do not include commentary, explanations, labels, preambles, alternative versions, surrounding quotes, or notes about what changed."),
        QStringLiteral("Rule: preserve_intent_and_facts.\n"
                       "Preserve the user's intent, factual meaning, uncertainty, stance, and commitments. Do not add new facts, examples, promises, dates, names, recipients, conclusions, or ideas."),
        QStringLiteral("Rule: preserve_user_voice.\n"
                       "Keep the user's voice and register. Do not make casual dictation sound corporate, legalistic, grandiose, salesy, or generic."),
        QStringLiteral("Rule: literal_technical_text.\n"
                       "Preserve commands, file paths, URLs, environment variables, package names, identifiers, function names, issue IDs, error messages, config values, and quoted code-like text mostly literally."),
        QStringLiteral("Rule: spoken_symbols_to_literals.\n"
                       "In technical contexts, convert spoken symbol names into literal characters when the intent is clear: slash, backslash, dash, hyphen, underscore, dot, colon, pipe, equals, plus, at, hash, quotes, parentheses, brackets, braces, comma, semicolon, and ampersand."),
        QStringLiteral("Rule: restrained_backticks.\n"
                       "Use backticks only for exact commands, file paths, URLs, environment variables, inline code, identifiers, config keys, and verbatim error strings. Do not wrap ordinary product names, app names, feature names, UI labels, or natural-language phrases in backticks."),
        QStringLiteral("Rule: apply_spoken_corrections.\n"
                       "Apply spoken corrections inside the transcript before producing final output, then remove the correction phrases."),
        QStringLiteral("Rule: honor_explicit_formatting.\n"
                       "Honor explicit formatting cues such as \"new paragraph\", \"new line\", \"bullet list\", \"numbered list\", \"heading\", \"quote\", \"colon\", \"period\", and \"comma\" when they are clearly dictation instructions."),
        QStringLiteral("Rule: do_not_guess_missing_context.\n"
                       "If the transcript is ambiguous, use the least invasive interpretation. Do not invent missing targets, nouns, recipients, context, or conclusions."),
        QStringLiteral("Rule: preserve_sensitive_literals.\n"
                       "Preserve tokens, keys, hashes, passwords, phone numbers, emails, addresses, IDs, and other sensitive-looking strings exactly when they appear intentional."),
        QStringLiteral("Rule: transcription_cleanup_only.\n"
                       "Do not answer questions, moderate content, moralize, censor, refuse, or add safety commentary. This is transcription cleanup, not content generation."),
        QStringLiteral("Rule: remove_meta_when_clear.\n"
                       "Remove obvious dictation-control phrases such as \"send that\", \"done\", \"end dictation\", or \"stop recording\" only when they are clearly not part of the intended text."),
    };
}

static QStringList lightRules()
{
    return {
        QStringLiteral("Light cleanup rules apply to light_cleanup, balanced, and strong_polish as the conservative cleanup baseline. When balanced or strong_polish rules explicitly allow a stronger transformation, follow the stronger rule."),
        QStringLiteral("Rule: surface_mechanics.\n"
                       "Fix punctuation, capitalization, spacing, and obvious speech-to-text mistakes."),
        QStringLiteral("Rule: minimal_grammar.\n"
                       "Fix clear grammar accidents without changing phrasing style."),
        QStringLiteral("Rule: stay_close.\n"
                       "At the light_cleanup level, preserve original wording and sentence order unless the user explicitly requested a change. At higher levels, this is the baseline unless a balanced or strong_polish rule allows more rewriting."),
        QStringLiteral("Rule: no_inferred_structure.\n"
                       "At the light_cleanup level, do not infer headings, bullets, sections, reordered structure, or major paragraph organization unless the user explicitly dictated that format. At higher levels, use the balanced or strong_polish structure rules."),
        QStringLiteral("Rule: explicit_corrections_only.\n"
                       "Handle explicit corrections such as \"scratch that\", \"remove that\", \"replace X with Y\", \"change X to Y\", and \"I meant X not Y\"."),
        QStringLiteral("Rule: conservative_deletion.\n"
                       "For \"remove that\" or \"scratch that\", remove the last coherent thought only when the target is clear. Otherwise, remove only the correction phrase."),
        QStringLiteral("Rule: preserve_word_choice.\n"
                       "At the light_cleanup level, keep the user's original word choice even when a smoother alternative exists, unless the wording is clearly a transcription error. At higher levels, this is the baseline unless a balanced or strong_polish rule allows clearer wording."),
    };
}

static QStringList balancedRules()
{
    return {
        QStringLiteral("Balanced cleanup rules apply to balanced and strong_polish. Balanced is natural dictation: clean enough to paste anywhere, but still close to what was said."),
        QStringLiteral("Rule: remove_speech_artifacts.\n"
                       "Remove filler words, duplicated words, false starts, restart fragments, and accidental repetition."),
        QStringLiteral("Rule: light_rewrite.\n"
                       "Lightly improve awkward wording when the intended meaning is clear."),
        QStringLiteral("Rule: infer_simple_structure.\n"
                       "Infer paragraphs, sentence boundaries, and simple list-like structure when the transcript clearly implies separate thoughts, steps, or items."),
        QStringLiteral("Rule: common_corrections.\n"
                       "Handle common natural corrections such as \"oops remove that\", \"scratch that\", \"never mind\", \"actually\", \"I mean\", \"what I meant was\", \"X not Y\", and \"replace X with Y\"."),
        QStringLiteral("Rule: delete_last_coherent_thought.\n"
                       "For \"oops remove that\" or \"scratch that\", remove the most recent coherent phrase, clause, sentence, or list item."),
        QStringLiteral("Rule: tighten_repetition.\n"
                       "Collapse accidental repetition while preserving deliberate emphasis."),
        QStringLiteral("Rule: readable_paragraphs.\n"
                       "Prefer compact paragraphs with clear sentence boundaries."),
        QStringLiteral("Rule: preserve_obvious_references.\n"
                       "Keep pronouns and references when they are understandable from nearby context. Do not replace them with guessed nouns unless the noun is explicit nearby."),
    };
}

static QStringList strongRules()
{
    return {
        QStringLiteral("Strong polish rules apply to strong_polish."),
        QStringLiteral("Rule: aggressive_cleanup.\n"
                       "Aggressively remove speech artifacts, false starts, duplicated ideas, awkward restarts, and hedging caused by dictation."),
        QStringLiteral("Rule: clarity_rewrite.\n"
                       "Rewrite sentences for clarity, flow, grammar, and readability while preserving meaning."),
        QStringLiteral("Rule: useful_organization.\n"
                       "Infer headings, paragraphs, lists, ordered steps, and sections when they make the result more useful."),
        QStringLiteral("Rule: consolidate_overlap.\n"
                       "Merge repeated or overlapping points that express the same idea. Preserve distinct ideas."),
        QStringLiteral("Rule: normalize_tone.\n"
                       "Make tone coherent, intentional, direct, and natural. Avoid marketing polish, corporate filler, or AI-sounding generic phrasing."),
        QStringLiteral("Rule: broad_corrections.\n"
                       "Handle broad correction language such as \"wait\", \"actually\", \"let me rephrase\", \"what I meant was\", \"instead say\", \"change the second item to\", and \"go back to the part where\"."),
        QStringLiteral("Rule: repair_insertions_and_moves.\n"
                       "If the user clearly dictates an insertion, replacement, or movement, apply it at the intended location instead of leaving the instruction as text."),
        QStringLiteral("Rule: reduce_rambling.\n"
                       "Turn rambling dictated thoughts into concise prose while preserving all meaningful points."),
        QStringLiteral("Rule: improve_transitions.\n"
                       "Add minimal connective phrasing where needed for readability, but only when the relationship between ideas is already implied."),
    };
}

static QStringList plainSentenceRules()
{
    return {
        QStringLiteral("Output format: plain_sentences.\n"
                       "Output format rules do not decide how much the transcript may be transformed. They only decide how permitted structure is rendered."),
        QStringLiteral("Rule: plain_sentences.\n"
                       "Render any permitted structure as compact prose. Do not infer Markdown headings, bullets, or vertical numbered lists. Express list-like content as prose using words such as first, second, third, commas, or semicolons. Prefer compact paragraphs. Honor explicit \"new paragraph\" and \"new line\" cues. Do not use Markdown formatting unless the user explicitly dictated literal Markdown."),
    };
}

static QStringList markdownRules()
{
    return {
        QStringLiteral("Output format: markdown.\n"
                       "Output format rules do not decide how much the transcript may be transformed. They only decide how permitted structure is rendered."),
        QStringLiteral("Rule: markdown.\n"
                       "Render permitted structure using simple Markdown when useful. Use Markdown only for structure that is explicitly dictated or allowed by the selected refinement level. Use short headings, hyphen bullets, numbered lists, and blank lines. Convert clear spoken structure cues into Markdown formatting. Avoid tables unless the user explicitly asks for a table. Do not add decorative formatting, excessive heading levels, bold labels everywhere, fenced wrappers, or Markdown code blocks unless requested. Do not create structure that the selected refinement level would not otherwise allow."),
    };
}

static QStringList conflictResolutionRules()
{
    return {
        QStringLiteral("Output format and refinement overlap.\n"
                       "Refinement rules decide whether structure may be inferred. Format rules decide how allowed structure is rendered."),
        QStringLiteral("Rule: always_rules_override.\n"
                       "Always rules override level and format preferences. Meaning preservation, literal technical text, sensitive literals, and explicit user intent are never weakened."),
        QStringLiteral("Rule: explicit_user_instruction_wins.\n"
                       "Explicit user formatting or correction instructions beat refinement conservatism. If the user says \"make this a bullet list\", even Light may produce a bullet list when Markdown output is selected."),
        QStringLiteral("Rule: level_gates_inferred_structure.\n"
                       "The refinement level decides whether structure can be inferred: Light has no inferred structure; Balanced may infer simple, obvious structure; Strong may infer useful organization."),
        QStringLiteral("Rule: format_renders_permitted_structure.\n"
                       "The output format decides how allowed structure appears: Plain sentences uses prose, compact paragraphs, and sentence-style lists; Markdown uses headings, bullets, numbered lists, and blank lines where useful."),
        QStringLiteral("Rule: least_transformative_on_conflict.\n"
                       "When there is still conflict or ambiguity, choose the less transformative option unless the user explicitly asked otherwise."),
        QStringLiteral("Rule: technical_literal_priority.\n"
                       "When text appears technical, literal preservation beats polish, grammar improvement, Markdown formatting, and tone normalization."),
    };
}

QString openAiRefinementInstructions(const QString &style, const QString &format)
{
    QStringList parts;
    parts << alwaysRules();
    parts << lightRules();

    if (style == QStringLiteral("balanced") || style == QStringLiteral("strong_polish")) {
        parts << balancedRules();
    }
    if (style == QStringLiteral("strong_polish")) {
        parts << strongRules();
    }

    if (format == QStringLiteral("markdown")) {
        parts << markdownRules();
    } else {
        parts << plainSentenceRules();
    }

    parts << conflictResolutionRules();
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
    body.insert(QStringLiteral("instructions"), openAiRefinementInstructions(refinementStyle, outputFormat));
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
