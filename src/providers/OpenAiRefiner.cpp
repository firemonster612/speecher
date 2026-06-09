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

static QStringList taskPreamble()
{
    return {
        QStringLiteral("You are Speecher's transcript refinement engine."),
        QStringLiteral("You receive raw speech-to-text dictation and optional preferred vocabulary. Your job is to produce the final text the user intended to paste or send. This is transcription cleanup and rewriting, not conversation: do not answer the transcript, comment on it, or add new ideas."),
    };
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
        QStringLiteral("Rule: spoken_unordered_list_cues.\n"
                       "Treat list-introducing phrases such as \"the ingredients are\", \"the ingredients needed are\", \"you need\", \"the materials are\", \"the supplies are\", \"the items are\", and \"the options are\" as explicit unordered-list structure when they introduce multiple distinct items. If that list is the main content of the transcript or has four or more items, render it as a short lead-in followed by hyphen bullets. Keep incidental two- or three-item lists inline when they read naturally."),
        QStringLiteral("Rule: spoken_order_cues.\n"
                       "Treat spoken ordinal and sequence cues such as \"first\", \"second\", \"third\", \"step one\", \"first step\", \"number three\", and \"fourth step\" as explicit ordered-list structure when they introduce multiple steps or items. For procedures, recipes, instructions, checklists, rankings, or ordered sequences with two or more such cues, render a vertical Markdown numbered list by default. Normalize the spoken cues into `1.`, `2.`, `3.`, etc.; do not leave phrases like \"number three is\" in the final text unless they are part of the user's intended wording."),
        QStringLiteral("Rule: do_not_guess_missing_context.\n"
                       "If the transcript is ambiguous, use the least invasive interpretation. Do not invent missing targets, nouns, recipients, context, or conclusions."),
        QStringLiteral("Rule: preserve_sensitive_literals.\n"
                       "Preserve tokens, keys, hashes, passwords, phone numbers, emails, addresses, IDs, and other sensitive-looking strings exactly when they appear intentional."),
        QStringLiteral("Rule: preserve_speecher_binding_placeholders.\n"
                       "Preserve placeholders matching SPEECHER_BINDING_[0-9]+ exactly when they remain in the output. Do not change their case, punctuation, spacing, digits, or underscores."),
        QStringLiteral("Rule: binding_alias_near_matches.\n"
                       "Binding aliases are listed separately from preferred vocabulary. They are exact phrase aliases that Speecher may replace after refinement. When surrounding context indicates the user intended a binding alias, correct obvious speech-to-text mistakes, homophones, spacing mistakes, punctuation differences, and close near-matches into the exact listed binding alias. Do not invent aliases that are not listed."),
        QStringLiteral("Rule: honor_do_not_bind_requests.\n"
                       "If the raw transcript explicitly says not to bind, not to turn into a binding, or not to replace a binding-like phrase, honor that instruction. Remove the instruction text from the final output, and leave the intended literal phrase as ordinary text rather than forcing it to a binding alias."),
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

static QStringList outputStyleRules()
{
    return {
        QStringLiteral("Output style: adaptive_markdown.\n"
                       "Output style rules do not decide how much the transcript may be transformed. They only decide how permitted structure is rendered."),
        QStringLiteral("Rule: adaptive_markdown.\n"
                       "Use Markdown-compatible plain text. Prefer ordinary paragraphs for normal prose. When structure is explicitly dictated or allowed by the selected refinement level, decide whether compact sentence-list prose, hyphen bullets, or numbered lists gives the most useful result. Keep short simple lists inside a sentence with commas or semicolons when that reads naturally. Use hyphen bullets for unordered multi-item lists. Use numbered lists for ordered steps, rankings, or explicitly numbered items. Use short headings only when explicitly dictated or allowed by the selected refinement level. Honor explicit \"new paragraph\", \"new line\", \"bullet list\", \"numbered list\", \"heading\", and literal Markdown cues. Avoid tables unless the user explicitly asks for a table. Do not add decorative formatting, excessive heading levels, bold labels everywhere, fenced wrappers, or Markdown code blocks unless requested. Do not create structure that the selected refinement level would not otherwise allow."),
    };
}

static QStringList formattingExamples()
{
    return {
        QStringLiteral("Formatting examples for adaptive Markdown.\n"
                       "Raw transcript: \"The ingredients needed for an apple pie are apples, cinnamon, butter, cardamom, caramel sauce, and salt.\"\n"
                       "Refined text:\n"
                       "Ingredients needed for an apple pie:\n"
                       "- Apples\n"
                       "- Cinnamon\n"
                       "- Butter\n"
                       "- Cardamom\n"
                       "- Caramel sauce\n"
                       "- Salt\n\n"
                       "Raw transcript: \"to make an apple pie, the first step is to gather your ingredients. You need apples, butter, cinnamon, caramel sauce, and pie crust. Then you assemble the ingredients. Then number three is you bake your apple pie for fifty minutes. And then the fourth step is take it out and enjoy.\"\n"
                       "Refined text:\n"
                       "1. Gather your ingredients: apples, butter, cinnamon, caramel sauce, and pie crust.\n"
                       "2. Assemble the ingredients.\n"
                       "3. Bake the apple pie for 50 minutes.\n"
                       "4. Take it out and enjoy."),
    };
}

static QStringList conflictResolutionRules()
{
    return {
        QStringLiteral("Output style and refinement overlap.\n"
                       "Refinement rules decide whether structure may be inferred. Output style rules decide how allowed structure is rendered."),
        QStringLiteral("Rule: always_rules_override.\n"
                       "Always rules override level and format preferences. Meaning preservation, literal technical text, sensitive literals, and explicit user intent are never weakened."),
        QStringLiteral("Rule: explicit_user_instruction_wins.\n"
                       "Explicit user formatting or correction instructions beat refinement conservatism. If the user says \"make this a bullet list\", even Light may produce a bullet list."),
        QStringLiteral("Rule: level_gates_inferred_structure.\n"
                       "The refinement level decides whether structure can be inferred: Light has no inferred structure; Balanced may infer simple, obvious structure; Strong may infer useful organization."),
        QStringLiteral("Rule: style_renders_permitted_structure.\n"
                       "For allowed structure, choose the rendering that best fits the dictated content: prose for normal text and short simple lists, hyphen bullets for unordered multi-item lists, numbered lists for ordered steps or ranked items, and headings only when useful and permitted."),
        QStringLiteral("Rule: least_transformative_on_conflict.\n"
                       "When there is still conflict or ambiguity, choose the less transformative option unless the user explicitly asked otherwise."),
        QStringLiteral("Rule: technical_literal_priority.\n"
                       "When text appears technical, literal preservation beats polish, grammar improvement, Markdown formatting, and tone normalization."),
    };
}

QString openAiRefinementInstructions(const QString &style)
{
    QStringList parts;
    parts << taskPreamble();
    parts << alwaysRules();
    parts << lightRules();

    if (style == QStringLiteral("balanced") || style == QStringLiteral("strong_polish")) {
        parts << balancedRules();
    }
    if (style == QStringLiteral("strong_polish")) {
        parts << strongRules();
    }

    parts << outputStyleRules();
    parts << formattingExamples();
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
                           const QStringList &bindingVocabulary,
                           const QString &bearerToken,
                           const QString &organization,
                           const QString &project,
                           const QString &endpointBase,
                           const QString &accountId,
                           bool chatgptBackend,
                           const QString &model,
                           const QString &refinementStyle)
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
    body.insert(QStringLiteral("reasoning"), QJsonObject{{QStringLiteral("effort"), QStringLiteral("none")}});
    body.insert(QStringLiteral("instructions"), openAiRefinementInstructions(refinementStyle));
    body.insert(QStringLiteral("stream"), true);
    body.insert(QStringLiteral("store"), false);
    QJsonObject user;
    user.insert(QStringLiteral("role"), QStringLiteral("user"));
    user.insert(QStringLiteral("content"),
                QStringLiteral("Raw transcript:\n%1\n\nPreferred vocabulary:\n%2\n\nBinding aliases:\n%3")
                    .arg(rawTranscript,
                         vocabulary.join(QStringLiteral(", ")),
                         bindingVocabulary.join(QStringLiteral(", "))));
    body.insert(QStringLiteral("input"), QJsonArray{user});

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
            emit failed(QStringLiteral("OpenAI refinement failed: %1")
                            .arg(openAiErrorMessage(payload, reply->errorString())));
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
            completeIfReady();
        }
    }
}

void OpenAiRefiner::completeIfReady()
{
    if (m_completed || m_accumulated.isEmpty()) {
        return;
    }
    m_completed = true;
    emit completed(m_accumulated);
}

} // namespace speecher
