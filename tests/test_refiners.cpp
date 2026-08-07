#include "common/test_doubles.h"
#include "common/test_http.h"
#include "common/test_auth.h"

using namespace speecher::test;


class RefinersTests : public QObject {
    Q_OBJECT

private slots:
    void refinementInstructionsCompose()
    {
        const QString light = dictationRefinementSystemPrompt(QStringLiteral("light_cleanup"));
        QVERIFY(light.startsWith(QStringLiteral("You are Speecher's transcript refinement engine.")));
        QVERIFY(light.contains(QStringLiteral("Output only the refined text. Do not add anything before or after it")));
        QVERIFY(light.contains(QStringLiteral("by following the rules below")));
        QVERIFY(light.contains(QStringLiteral("Your job is to produce the final text the user intended to paste or send by following the rules below.")));
        QVERIFY(light.contains(QStringLiteral("This is transcription cleanup and rewriting, not conversation")));
        QVERIFY(light.contains(QStringLiteral("Preferred vocabulary is a list of terms that may be relevant to the user's dictation")));
        QVERIFY(light.contains(QStringLiteral("Use preferred vocabulary as context to correct likely speech-to-text mistakes")));
        QVERIFY(light.contains(QStringLiteral("Do not force preferred vocabulary into the output")));
        QVERIFY(light.contains(QStringLiteral("Binding aliases are exact spoken phrases that may be matched after refinement")));
        QVERIFY(light.contains(QStringLiteral("Do not output binding replacement values")));
        QVERIFY(light.contains(QStringLiteral("Rule: return_only_refined_text.")));
        QVERIFY(light.contains(QStringLiteral("Rule: preserve_speecher_binding_placeholders.")));
        QVERIFY(light.contains(QStringLiteral("SPEECHER_BINDING_[0-9]+")));
        QVERIFY(light.contains(QStringLiteral("Rule: binding_alias_near_matches.")));
        QVERIFY(light.contains(QStringLiteral("exact phrases, not replacement text")));
        QVERIFY(light.contains(QStringLiteral("correct obvious speech-to-text mistakes")));
        QVERIFY(light.contains(QStringLiteral("Rule: honor_do_not_bind_requests.")));
        QVERIFY(light.contains(QStringLiteral("Remove the instruction text")));
        QVERIFY(light.contains(QStringLiteral("Rule: spoken_unordered_list_cues.")));
        QVERIFY(light.contains(QStringLiteral("render it as a short lead-in followed by hyphen bullets")));
        QVERIFY(light.contains(QStringLiteral("Rule: spoken_order_cues.")));
        QVERIFY(light.contains(QStringLiteral("For procedures, recipes, instructions, checklists, rankings, or ordered sequences")));
        QVERIFY(light.contains(QStringLiteral("render a vertical Markdown numbered list by default")));
        QVERIFY(light.contains(QStringLiteral("Rule: no_inferred_structure.")));
        QVERIFY(light.contains(QStringLiteral("Output style: adaptive_markdown.")));
        QVERIFY(light.contains(QStringLiteral("Keep short simple lists inside a sentence with commas or semicolons")));
        QVERIFY(light.contains(QStringLiteral("Ingredients needed for an apple pie:\n- Apples\n- Cinnamon")));
        QVERIFY(light.contains(QStringLiteral("1. Gather your ingredients: apples, butter, cinnamon, caramel sauce, and pie crust.")));
        QVERIFY(light.contains(QStringLiteral("even Light may produce a bullet list")));
        QVERIFY(!light.contains(QStringLiteral("Rule: infer_simple_structure.")));
        QVERIFY(!light.contains(QStringLiteral("Rule: useful_organization.")));
        QVERIFY(!light.contains(QStringLiteral("plain_sentences")));

        const QString balanced = dictationRefinementSystemPrompt(QStringLiteral("balanced"));
        QVERIFY(balanced.contains(QStringLiteral("Rule: no_inferred_structure.")));
        QVERIFY(balanced.contains(QStringLiteral("Rule: infer_simple_structure.")));
        QVERIFY(balanced.contains(QStringLiteral("Rule: adaptive_markdown.")));
        QVERIFY(balanced.contains(QStringLiteral("Use hyphen bullets for unordered multi-item lists.")));
        QVERIFY(!balanced.contains(QStringLiteral("Rule: useful_organization.")));

        const QString strong = dictationRefinementSystemPrompt(QStringLiteral("strong_polish"));
        QVERIFY(strong.contains(QStringLiteral("Rule: no_inferred_structure.")));
        QVERIFY(strong.contains(QStringLiteral("Rule: infer_simple_structure.")));
        QVERIFY(strong.contains(QStringLiteral("Rule: useful_organization.")));
        QVERIFY(strong.contains(QStringLiteral("Rule: technical_literal_priority.")));

        RefinementContext editingContext;
        editingContext.editSelection = true;
        editingContext.includeNearbyText = true;
        editingContext.writingProfile = WritingProfile::Email;
        editingContext.tone = QStringLiteral("formal");
        editingContext.target.applicationId = QStringLiteral("dev.t3code.T3Code");
        editingContext.target.applicationName = QStringLiteral("T3 Code");
        editingContext.target.category = AppCategory::CodeEditor;
        editingContext.target.role = QStringLiteral("text");
        editingContext.target.windowTitle = QStringLiteral("Benjamin email");
        editingContext.target.nearbyTextBefore = QStringLiteral("Previous paragraph");
        editingContext.target.nearbyTextAfter = QStringLiteral("Next paragraph");
        editingContext.target.selectedText = QStringLiteral("Dear Benjamin, please remove the cockroaches.");
        editingContext.target.selectionStart = 10;
        editingContext.target.selectionEnd = 57;
        const QString editing = selectedDocumentEditingSystemPrompt(
            QStringLiteral("strong_polish"),
            editingContext);
        QVERIFY(editing.startsWith(QStringLiteral("You are Speecher's document editor and writer.")));
        QVERIFY(!editing.contains(QStringLiteral("transcript refinement engine")));
        QVERIFY(editing.contains(QStringLiteral("selected document is the authoritative source")));
        QVERIFY(editing.contains(QStringLiteral("spoken editing instructions")));
        QVERIFY(editing.contains(QStringLiteral("Return only the complete revised document")));
        QVERIFY(editing.contains(QStringLiteral("Strong polish permits substantial rewriting")));
        QVERIFY(editing.contains(QStringLiteral("\"refinement_style\":\"strong_polish\"")));
        QVERIFY(editing.contains(QStringLiteral("\"writing_profile\":\"email\"")));
        QVERIFY(editing.contains(QStringLiteral("\"application_name\":\"T3 Code\"")));
        QVERIFY(editing.contains(QStringLiteral("\"text_before_caret\":\"Previous paragraph\"")));
        QVERIFY(!editing.contains(editingContext.target.selectedText));
    }

    void openAiRefinerSendsAdaptiveInstructions()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));

        OpenAiRefiner refiner;
        QSignalSpy completed(&refiner, &OpenAiRefiner::completed);
        QSignalSpy failed(&refiner, &OpenAiRefiner::failed);

        const QString rawTranscript = QStringLiteral("to make an apple pie, the first step is to gather your ingredients. You need apples, butter, cinnamon, caramel sauce, and pie crust. Then you assemble the ingredients. Then number three is you bake your apple pie for fifty minutes. And then the fourth step is take it out and enjoy.");
        RefinementContext context;
        context.includeNearbyText = true;
        context.writingProfile = WritingProfile::Work;
        context.tone = QStringLiteral("formal");
        context.target.applicationId = QStringLiteral("dev.t3code.T3Code");
        context.target.applicationName = QStringLiteral("T3 Code");
        context.target.category = AppCategory::CodeEditor;
        context.target.role = QStringLiteral("text");
        context.target.windowTitle = QStringLiteral("Recipe notes");
        context.target.nearbyTextBefore = QStringLiteral("Dinner plan");
        context.target.nearbyTextAfter = QStringLiteral("Shopping list");
        context.screenshotData = QByteArrayLiteral("png-bytes");
        context.screenshotMediaType = QStringLiteral("image/png");
        refiner.refine(rawTranscript,
                       QStringList{QStringLiteral("Qt"), QStringLiteral("Pie crust")},
                       QStringList{QStringLiteral("my email"), QStringLiteral("speecher repo")},
                       QStringLiteral("test-token"),
                       QStringLiteral("org-id"),
                       QStringLiteral("project-id"),
                       QStringLiteral("http://127.0.0.1:%1/v1/").arg(server.serverPort()),
                       QStringLiteral("acct-id"),
                       true,
                       QStringLiteral("gpt-test"),
                       QStringLiteral("high"),
                       QStringLiteral("balanced"),
                       context);

        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
        QTcpSocket *socket = server.nextPendingConnection();
        QVERIFY(socket);

        const QByteArray request = readHttpRequest(socket, 1000);
        const int headerEnd = request.indexOf("\r\n\r\n");
        QVERIFY2(headerEnd >= 0, request.constData());
        const QByteArray headers = request.left(headerEnd);
        const int contentLength = httpContentLength(headers);
        QVERIFY(contentLength > 0);
        QVERIFY(request.size() >= headerEnd + 4 + contentLength);

        QCOMPARE(headers.left(headers.indexOf('\n')).trimmed(), QByteArrayLiteral("POST /v1/responses HTTP/1.1"));
        const QByteArray lowerHeaders = headers.toLower();
        QVERIFY(lowerHeaders.contains(QByteArrayLiteral("authorization: bearer test-token")));
        QVERIFY(lowerHeaders.contains(QByteArrayLiteral("openai-organization: org-id")));
        QVERIFY(lowerHeaders.contains(QByteArrayLiteral("openai-project: project-id")));
        QVERIFY(lowerHeaders.contains(QByteArrayLiteral("chatgpt-account-id: acct-id")));

        QJsonParseError parseError;
        const QByteArray payload = request.mid(headerEnd + 4, contentLength);
        const QJsonObject body = QJsonDocument::fromJson(payload, &parseError).object();
        QCOMPARE(parseError.error, QJsonParseError::NoError);
        QCOMPARE(body.value(QStringLiteral("model")).toString(), QStringLiteral("gpt-test"));
        QCOMPARE(body.value(QStringLiteral("reasoning")).toObject().value(QStringLiteral("effort")).toString(), QStringLiteral("high"));
        QCOMPARE(body.value(QStringLiteral("stream")).toBool(), true);
        QCOMPARE(body.value(QStringLiteral("store")).toBool(), false);

        const QString instructions = body.value(QStringLiteral("instructions")).toString();
        QVERIFY(instructions.startsWith(QStringLiteral("You are Speecher's transcript refinement engine.")));
        QVERIFY(!instructions.contains(QStringLiteral("document editor and writer")));
        QVERIFY(instructions.contains(QStringLiteral("\"refinement_style\":\"balanced\"")));
        QVERIFY(instructions.contains(QStringLiteral("\"writing_profile\":\"work\"")));
        QVERIFY(instructions.contains(QStringLiteral("\"application_name\":\"T3 Code\"")));
        QVERIFY(instructions.contains(QStringLiteral("\"text_before_caret\":\"Dinner plan\"")));
        QVERIFY(instructions.contains(QStringLiteral("Rule: preserve_speecher_binding_placeholders.")));
        QVERIFY(instructions.contains(QStringLiteral("Do not change their case, punctuation, spacing, digits, or underscores.")));
        QVERIFY(instructions.contains(QStringLiteral("Rule: binding_alias_near_matches.")));
        QVERIFY(instructions.contains(QStringLiteral("Rule: honor_do_not_bind_requests.")));
        QVERIFY(instructions.contains(QStringLiteral("Rule: spoken_unordered_list_cues.")));
        QVERIFY(instructions.contains(QStringLiteral("If that list is the main content of the transcript or has four or more items")));
        QVERIFY(instructions.contains(QStringLiteral("Ingredients needed for an apple pie:\n- Apples\n- Cinnamon")));
        QVERIFY(instructions.contains(QStringLiteral("Rule: spoken_order_cues.")));
        QVERIFY(instructions.contains(QStringLiteral("render a vertical Markdown numbered list by default")));
        QVERIFY(instructions.contains(QStringLiteral("1. Gather your ingredients: apples, butter, cinnamon, caramel sauce, and pie crust.")));
        QVERIFY(!instructions.contains(QStringLiteral("plain_sentences")));

        const QJsonArray input = body.value(QStringLiteral("input")).toArray();
        QCOMPARE(input.size(), 1);
        const QJsonObject user = input.at(0).toObject();
        QCOMPARE(user.value(QStringLiteral("role")).toString(), QStringLiteral("user"));
        const QJsonArray contentBlocks = user.value(QStringLiteral("content")).toArray();
        QCOMPARE(contentBlocks.size(), 2);
        QCOMPARE(contentBlocks.at(0).toObject().value(QStringLiteral("type")).toString(),
                 QStringLiteral("input_text"));
        const QString content = contentBlocks.at(0).toObject().value(QStringLiteral("text")).toString();
        QVERIFY(content.contains(rawTranscript));
        QVERIFY(content.contains(QStringLiteral("Preferred vocabulary:\nQt, Pie crust")));
        QVERIFY(content.contains(QStringLiteral("Binding aliases:\nmy email, speecher repo")));
        const QJsonObject image = contentBlocks.at(1).toObject();
        QCOMPARE(image.value(QStringLiteral("type")).toString(), QStringLiteral("input_image"));
        QCOMPARE(image.value(QStringLiteral("detail")).toString(), QStringLiteral("low"));
        QCOMPARE(image.value(QStringLiteral("image_url")).toString(),
                 QStringLiteral("data:image/png;base64,cG5nLWJ5dGVz"));

        const QByteArray sse = QByteArrayLiteral("event: response.output_text.delta\n"
                                                 "data: {\"delta\":\"1. Gather\"}\n\n"
                                                 "event: response.completed\n"
                                                 "data: {\"type\":\"response.completed\"}\n\n");
        socket->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\n"
                                        "Content-Type: text/event-stream\r\n"
                                        "Content-Length: ")
                      + QByteArray::number(sse.size())
                      + QByteArrayLiteral("\r\n"
                                          "Connection: close\r\n"
                                          "\r\n")
                      + sse);
        QVERIFY(socket->waitForBytesWritten(1000));
        socket->disconnectFromHost();

        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 1000);
        QCOMPARE(completed.at(0).at(0).toString(), QStringLiteral("1. Gather"));
        QTest::qWait(50);
        QCOMPARE(completed.size(), 1);
        QCOMPARE(failed.size(), 0);
    }

    void openAiRefinerFailsWhenResponseStalls()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));

        OpenAiRefiner refiner(nullptr, 50);
        QSignalSpy completed(&refiner, &OpenAiRefiner::completed);
        QSignalSpy failed(&refiner, &OpenAiRefiner::failed);

        refiner.refine(QStringLiteral("Make this formal"),
                       {},
                       {},
                       QStringLiteral("test-token"),
                       {},
                       {},
                       QStringLiteral("http://127.0.0.1:%1/v1/").arg(server.serverPort()),
                       {},
                       false,
                       QStringLiteral("gpt-test"),
                       QStringLiteral("low"),
                       QStringLiteral("balanced"),
                       {});

        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
        QTcpSocket *socket = server.nextPendingConnection();
        QVERIFY(socket);
        QVERIFY(!readHttpRequest(socket, 1000).isEmpty());

        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 1000);
        QVERIFY(failed.at(0).at(0).toString().contains(QStringLiteral("timed out")));
        QCOMPARE(completed.size(), 0);
    }

    void anthropicApiRefinerSendsClaudeCodeOauthShape()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));

        AnthropicApiRefiner refiner;
        QSignalSpy completed(&refiner, &AnthropicApiRefiner::completed);
        QSignalSpy failed(&refiner, &AnthropicApiRefiner::failed);

        const QString rawTranscript = QStringLiteral("Make this much longer and more formal");
        RefinementContext context;
        context.editSelection = true;
        context.includeNearbyText = true;
        context.writingProfile = WritingProfile::Work;
        context.tone = QStringLiteral("formal");
        context.target.applicationName = QStringLiteral("T3 Code");
        context.target.category = AppCategory::CodeEditor;
        context.target.role = QStringLiteral("text");
        context.target.nearbyTextBefore = QStringLiteral("Unrelated project update");
        context.target.nearbyTextAfter = QStringLiteral("Unrelated issue status");
        context.target.selectedText = QStringLiteral("Dear Benjamin, the ostriches are well, but please remove the cockroaches from the ceiling.");
        context.target.selectionStart = 0;
        context.target.selectionEnd = context.target.selectedText.size();
        context.screenshotData = QByteArrayLiteral("png-bytes");
        context.screenshotMediaType = QStringLiteral("image/png");
        refiner.refine(rawTranscript,
                       QStringList{QStringLiteral("Qt")},
                       QStringList{QStringLiteral("my email")},
                       QStringLiteral("test-token"),
                       QStringLiteral("http://127.0.0.1:%1/v1/").arg(server.serverPort()),
                       QStringLiteral("claude-sonnet-4-6"),
                       QStringLiteral("low"),
                       QStringLiteral("balanced"),
                       context);

        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
        QTcpSocket *socket = server.nextPendingConnection();
        QVERIFY(socket);

        const QByteArray request = readHttpRequest(socket, 1000);
        const int headerEnd = request.indexOf("\r\n\r\n");
        QVERIFY2(headerEnd >= 0, request.constData());
        const QByteArray headers = request.left(headerEnd);
        const int contentLength = httpContentLength(headers);
        QVERIFY(contentLength > 0);
        QVERIFY(request.size() >= headerEnd + 4 + contentLength);

        QCOMPARE(headers.left(headers.indexOf('\n')).trimmed(), QByteArrayLiteral("POST /v1/messages HTTP/1.1"));
        const QByteArray lowerHeaders = headers.toLower();
        QVERIFY(lowerHeaders.contains(QByteArrayLiteral("authorization: bearer test-token")));
        QVERIFY(lowerHeaders.contains(QByteArrayLiteral("anthropic-version: 2023-06-01")));
        QVERIFY(lowerHeaders.contains(QByteArrayLiteral("anthropic-beta: claude-code-20250219,oauth-2025-04-20")));
        QVERIFY(lowerHeaders.contains(QByteArrayLiteral("user-agent: claude-cli/")));
        QVERIFY(lowerHeaders.contains(QByteArrayLiteral("(external, cli)")));
        QVERIFY(lowerHeaders.contains(QByteArrayLiteral("x-app: cli")));
        QVERIFY(lowerHeaders.contains(QByteArrayLiteral("x-claude-code-session-id:")));
        QVERIFY(lowerHeaders.contains(QByteArrayLiteral("x-client-request-id:")));

        QJsonParseError parseError;
        const QByteArray payload = request.mid(headerEnd + 4, contentLength);
        const QJsonObject body = QJsonDocument::fromJson(payload, &parseError).object();
        QCOMPARE(parseError.error, QJsonParseError::NoError);
        QCOMPARE(body.value(QStringLiteral("model")).toString(), QStringLiteral("claude-sonnet-4-6"));
        QCOMPARE(body.value(QStringLiteral("thinking")).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("adaptive"));
        QCOMPARE(body.value(QStringLiteral("thinking")).toObject().value(QStringLiteral("display")).toString(), QStringLiteral("omitted"));
        QCOMPARE(body.value(QStringLiteral("output_config")).toObject().value(QStringLiteral("effort")).toString(), QStringLiteral("low"));
        QCOMPARE(body.value(QStringLiteral("stream")).toBool(), true);

        const QString system = body.value(QStringLiteral("system")).toString();
        QVERIFY(system.startsWith(QStringLiteral("You are Claude Code, Anthropic's official CLI for Claude.")));
        QVERIFY(system.contains(QStringLiteral("You are Speecher's document editor and writer.")));
        QVERIFY(!system.contains(QStringLiteral("transcript refinement engine")));
        QVERIFY(system.contains(QStringLiteral("selected document is the authoritative source")));
        QVERIFY(system.contains(QStringLiteral("\"writing_profile\":\"work\"")));
        QVERIFY(system.contains(QStringLiteral("\"application_name\":\"T3 Code\"")));
        QVERIFY(system.contains(QStringLiteral("\"text_before_caret\":\"Unrelated project update\"")));
        QVERIFY(!system.contains(context.target.selectedText));

        const QJsonArray messages = body.value(QStringLiteral("messages")).toArray();
        QCOMPARE(messages.size(), 1);
        const QJsonObject user = messages.at(0).toObject();
        QCOMPARE(user.value(QStringLiteral("role")).toString(), QStringLiteral("user"));
        const QString content = user.value(QStringLiteral("content")).toString();
        QVERIFY(content.contains(rawTranscript));
        QVERIFY(content.contains(context.target.selectedText));
        QVERIFY(content.contains(QStringLiteral("selected_document")));
        QVERIFY(content.contains(QStringLiteral("spoken_editing_instructions")));
        QVERIFY(content.contains(QStringLiteral("Preferred vocabulary:\nQt")));
        QVERIFY(content.contains(QStringLiteral("Binding aliases:\nmy email")));

        const QByteArray sse = QByteArrayLiteral("event: content_block_delta\n"
                                                 "data: {\"type\":\"content_block_delta\",\"delta\":{\"type\":\"text_delta\",\"text\":\"oauth-ok\"}}\n\n"
                                                 "event: message_stop\n"
                                                 "data: {\"type\":\"message_stop\"}\n\n");
        socket->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\n"
                                        "Content-Type: text/event-stream\r\n"
                                        "Content-Length: ")
                      + QByteArray::number(sse.size())
                      + QByteArrayLiteral("\r\n"
                                          "Connection: close\r\n"
                                          "\r\n")
                      + sse);
        QVERIFY(socket->waitForBytesWritten(1000));
        socket->disconnectFromHost();

        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 1000);
        QCOMPARE(completed.at(0).at(0).toString(), QStringLiteral("oauth-ok"));
        QCOMPARE(failed.size(), 0);
    }

    void anthropicApiRefinerDoesNotTreatUnavailableModelsAsEffortSupported()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));

        AnthropicApiRefiner refiner;
        refiner.refine(QStringLiteral("please clean this up"),
                       {},
                       {},
                       QStringLiteral("test-token"),
                       QStringLiteral("http://127.0.0.1:%1/v1/").arg(server.serverPort()),
                       QStringLiteral("claude-mythos-5"),
                       QStringLiteral("low"),
                       QStringLiteral("balanced"),
                       {});

        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
        QTcpSocket *socket = server.nextPendingConnection();
        QVERIFY(socket);

        const QByteArray request = readHttpRequest(socket, 1000);
        const int headerEnd = request.indexOf("\r\n\r\n");
        QVERIFY2(headerEnd >= 0, request.constData());
        const QByteArray headers = request.left(headerEnd);
        const int contentLength = httpContentLength(headers);
        QVERIFY(contentLength > 0);
        QVERIFY(request.size() >= headerEnd + 4 + contentLength);

        QJsonParseError parseError;
        const QByteArray payload = request.mid(headerEnd + 4, contentLength);
        const QJsonObject body = QJsonDocument::fromJson(payload, &parseError).object();
        QCOMPARE(parseError.error, QJsonParseError::NoError);
        QCOMPARE(body.value(QStringLiteral("model")).toString(), QStringLiteral("claude-mythos-5"));
        QVERIFY(!body.contains(QStringLiteral("thinking")));
        QVERIFY(!body.contains(QStringLiteral("output_config")));

        const QByteArray sse = QByteArrayLiteral("event: content_block_delta\n"
                                                 "data: {\"type\":\"content_block_delta\",\"delta\":{\"type\":\"text_delta\",\"text\":\"ok\"}}\n\n"
                                                 "event: message_stop\n"
                                                 "data: {\"type\":\"message_stop\"}\n\n");
        socket->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\n"
                                        "Content-Type: text/event-stream\r\n"
                                        "Content-Length: ")
                      + QByteArray::number(sse.size())
                      + QByteArrayLiteral("\r\n"
                                          "Connection: close\r\n"
                                          "\r\n")
                      + sse);
        QVERIFY(socket->waitForBytesWritten(1000));
        socket->disconnectFromHost();
    }
};

int runRefinersTests(int argc, char **argv)
{
    RefinersTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_refiners.moc"
