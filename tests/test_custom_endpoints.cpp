#include "common/test_prelude.h"
#include "common/test_http.h"
#include "core/settings/SettingsKeys.h"
#include "providers/ChatCompletionsRefiner.h"
#include "providers/CustomEndpoints.h"
#include "providers/EndpointSpeechTranscriber.h"
#include "providers/EndpointTranscriptRefiner.h"
#include "providers/LocalRunner.h"

#include <QHttpMultiPart>
#include <QScopeGuard>
#include <QNetworkAccessManager>
#include <QNetworkReply>

using namespace speecher::test;

namespace {

QByteArray httpResponse(const QByteArray &status, const QByteArray &contentType, const QByteArray &body)
{
    return "HTTP/1.1 " + status + "\r\nContent-Type: " + contentType + "\r\nContent-Length: "
        + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
}

QByteArray json(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

// readHttpRequest waits out its timeout on a request without a body.
QByteArray readRequest(QTcpSocket *socket)
{
    QByteArray request;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 2000) {
        request += socket->readAll();
        const int headerEnd = request.indexOf("\r\n\r\n");
        if (headerEnd >= 0
            && request.size() >= headerEnd + 4 + qMax(0, httpContentLength(request.left(headerEnd)))) {
            return request;
        }
        socket->waitForReadyRead(20);
    }
    return request;
}

// Answers each request with the route's canned response and records the
// requests it saw.
class FakeServer : public QObject {
public:
    FakeServer()
    {
        QVERIFY(m_server.listen(QHostAddress::LocalHost));
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                const QByteArray request = readRequest(socket);
                requests << request;
                const QByteArray line = request.left(request.indexOf("\r\n"));
                const QByteArray route = line.split(' ').value(0) + ' ' + line.split(' ').value(1);
                const auto next = [this, &route] {
                    QList<QByteArray> &queued = m_routes[route];
                    return queued.size() > 1 ? queued.takeFirst() : queued.value(0);
                };
                const QByteArray response = m_routes.contains(route)
                    ? next()
                    : httpResponse("404 Not Found", "text/plain", "404 page not found");
                socket->write(response);
                socket->flush();
                socket->disconnectFromHost();
            }
        });
    }

    // Responses for one route are used in order; the last one repeats.
    void route(const QByteArray &methodAndPath, const QByteArray &response)
    {
        m_routes[methodAndPath] << response;
    }

    QString origin() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }

    QJsonObject jsonBody(int index) const
    {
        const QByteArray &request = requests.at(index);
        return QJsonDocument::fromJson(request.mid(request.indexOf("\r\n\r\n") + 4)).object();
    }

    QList<QByteArray> requests;

private:
    QTcpServer m_server;
    QHash<QByteArray, QList<QByteArray>> m_routes;
};

QByteArray sse(const QList<QByteArray> &dataLines)
{
    QByteArray body;
    for (const QByteArray &data : dataLines) body += "data: " + data + "\n\n";
    return httpResponse("200 OK", "text/event-stream", body);
}

QByteArray chatChunk(const QString &content, const QString &finishReason = {})
{
    QJsonObject choice{{QStringLiteral("index"), 0},
                       {QStringLiteral("delta"), QJsonObject{{QStringLiteral("content"), content}}}};
    if (!finishReason.isEmpty()) choice.insert(QStringLiteral("finish_reason"), finishReason);
    return json({{QStringLiteral("choices"), QJsonArray{choice}}});
}

QByteArray headersOf(const QByteArray &request)
{
    return request.left(request.indexOf("\r\n\r\n")).toLower();
}

} // namespace

class CustomEndpointsTests : public QObject {
    Q_OBJECT

private slots:
    void speechUploadIsA16kMonoWavWithTheOpenAiFields()
    {
        SpeechEndpointSettings endpoint;
        endpoint.baseUrl = QStringLiteral("http://127.0.0.1:1");
        endpoint.path = QStringLiteral("/inference");
        endpoint.model = QStringLiteral("whisper-large-v3-turbo");
        endpoint.apiKey = QStringLiteral("speech-key");
        const QByteArray pcm(3200, '\x01');

        FakeServer server;
        server.route("POST /inference", httpResponse("200 OK", "application/json", "{\"text\":\"ok\"}"));
        endpoint.baseUrl = server.origin();
        const SpeechEndpointUpload upload = speechEndpointUpload(endpoint, pcm);
        QNetworkAccessManager network;
        QNetworkReply *reply = network.post(upload.request, upload.parts);
        upload.parts->setParent(reply);
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
        reply->deleteLater();

        QCOMPARE(server.requests.size(), 1);
        const QByteArray request = server.requests.first();
        QVERIFY(request.startsWith("POST /inference HTTP/1.1"));
        QVERIFY(headersOf(request).contains("authorization: bearer speech-key"));
        QVERIFY(headersOf(request).contains("content-type: multipart/form-data"));
        const QByteArray body = request.mid(request.indexOf("\r\n\r\n") + 4);
        QVERIFY(body.contains("name=\"file\"; filename=\"dictation.wav\""));
        QVERIFY(body.toLower().contains("content-type: audio/wav"));
        // RIFF header for 16 kHz, mono, 16-bit, followed by the samples.
        const int wav = body.indexOf("RIFF");
        QVERIFY(wav >= 0);
        QCOMPARE(body.mid(wav + 22, 2), QByteArray("\x01\x00", 2));
        QCOMPARE(body.mid(wav + 24, 4), QByteArray("\x80\x3e\x00\x00", 4));
        QCOMPARE(body.mid(wav + 34, 2), QByteArray("\x10\x00", 2));
        QCOMPARE(body.mid(wav + 44, pcm.size()), pcm);
        for (const QByteArray field : {QByteArray("model\"\r\n\r\nwhisper-large-v3-turbo"),
                                       QByteArray("response_format\"\r\n\r\njson"),
                                       QByteArray("language\"\r\n\r\nen"),
                                       QByteArray("stream\"\r\n\r\ntrue")}) {
            QVERIFY2(body.contains(field), field.constData());
        }
    }

    void speechEndpointReadsAJsonTranscriptOnce()
    {
        FakeServer server;
        server.route("POST /v1/audio/transcriptions",
                     httpResponse("200 OK", "application/json", "{\"text\":\" Hello there. \"}"));
        SpeechSettings settings;
        settings.endpoint.baseUrl = server.origin();
        EndpointSpeechTranscriber transcriber;
        QSignalSpy transcript(&transcriber, &SpeechTranscriber::attemptTranscript);
        QSignalSpy completed(&transcriber, &SpeechTranscriber::attemptCompleted);

        transcriber.startAttempt(7, settings);
        transcriber.sendAudio(7, QByteArray(640, '\0'));
        transcriber.sendAudio(7, QByteArray(640, '\0'));
        transcriber.finishInput(7);

        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 2000);
        QCOMPARE(transcript.size(), 1);
        QCOMPARE(transcript.first().at(0).toULongLong(), 7ull);
        QCOMPARE(transcript.first().at(1).toString(), QStringLiteral("Hello there."));
        QCOMPARE(server.requests.size(), 1);
    }

    void speechEndpointReadsAStreamedTranscript()
    {
        FakeServer server;
        const QByteArray stream =
            "event: transcript.text.delta\ndata: {\"type\":\"transcript.text.delta\",\"delta\":\"Hello\"}\n\n"
            "data: {\"type\":\"transcript.text.delta\",\"delta\":\" world\"}\n\n"
            "data: {\"type\":\"transcript.text.done\",\"text\":\"Hello, world.\"}\n\n";
        server.route("POST /v1/audio/transcriptions", httpResponse("200 OK", "text/event-stream", stream));
        SpeechSettings settings;
        settings.endpoint.baseUrl = server.origin();
        EndpointSpeechTranscriber transcriber;
        QSignalSpy partial(&transcriber, &SpeechTranscriber::partialTranscript);
        QSignalSpy transcript(&transcriber, &SpeechTranscriber::attemptTranscript);
        QSignalSpy completed(&transcriber, &SpeechTranscriber::attemptCompleted);

        transcriber.startAttempt(1, settings);
        transcriber.sendAudio(1, QByteArray(640, '\0'));
        transcriber.finishInput(1);

        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 2000);
        QCOMPARE(partial.last().at(1).toString(), QStringLiteral("Hello world"));
        QCOMPARE(transcript.size(), 1);
        QCOMPARE(transcript.first().at(1).toString(), QStringLiteral("Hello, world."));
    }

    // Rule A7: a failed upload fails the attempt and is not sent again.
    void speechEndpointFailureIsNotRetried()
    {
        FakeServer server;
        server.route("POST /v1/audio/transcriptions",
                     httpResponse("500 Internal Server Error", "application/json",
                                  "{\"error\":{\"message\":\"model not loaded\"}}"));
        SpeechSettings settings;
        settings.endpoint.baseUrl = server.origin();
        EndpointSpeechTranscriber transcriber;
        QSignalSpy failed(&transcriber, &SpeechTranscriber::failed);
        QSignalSpy completed(&transcriber, &SpeechTranscriber::attemptCompleted);

        transcriber.startAttempt(3, settings);
        transcriber.sendAudio(3, QByteArray(640, '\0'));
        transcriber.finishInput(3);

        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 2000);
        const SpeechFailure failure = failed.first().first().value<SpeechFailure>();
        QVERIFY2(failure.message.contains(QStringLiteral("model not loaded")), qPrintable(failure.message));
        QVERIFY(!failure.retryable);
        QTest::qWait(100);
        QCOMPARE(server.requests.size(), 1);
        QCOMPARE(completed.size(), 0);
    }

    // The server keeps the connection busy past the inactivity limit, then
    // goes quiet: the streamed text so far is the dictation.
    void speechEndpointKeepsStreamingPastTheInactivityLimitAndKeepsTextWhenItStalls()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        SpeechSettings settings;
        settings.endpoint.baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
        EndpointSpeechTranscriber transcriber(nullptr, 300, 5000);
        QSignalSpy transcript(&transcriber, &SpeechTranscriber::attemptTranscript);
        QSignalSpy completed(&transcriber, &SpeechTranscriber::attemptCompleted);
        QSignalSpy failed(&transcriber, &SpeechTranscriber::failed);

        transcriber.startAttempt(1, settings);
        transcriber.sendAudio(1, QByteArray(640, '\0'));
        transcriber.finishInput(1);
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 2000);
        QTcpSocket *socket = server.nextPendingConnection();
        QVERIFY(!readRequest(socket).isEmpty());
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n\r\n");
        for (const char *word : {"One", " two", " three", " four"}) {
            socket->write("data: {\"type\":\"transcript.text.delta\",\"delta\":\"" + QByteArray(word) + "\"}\n\n");
            socket->flush();
            QTest::qWait(150);
        }
        QCOMPARE(failed.size(), 0);
        QCOMPARE(completed.size(), 0);

        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 2000);
        QCOMPARE(failed.size(), 0);
        QCOMPARE(transcript.size(), 1);
        QCOMPARE(transcript.first().at(1).toString(), QStringLiteral("One two three four"));
    }

    void speechEndpointFailsWhenAQuietServerStallsBeforeAnyText()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        SpeechSettings settings;
        settings.endpoint.baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
        EndpointSpeechTranscriber transcriber(nullptr, 200, 5000);
        QSignalSpy failed(&transcriber, &SpeechTranscriber::failed);
        QSignalSpy completed(&transcriber, &SpeechTranscriber::attemptCompleted);

        transcriber.startAttempt(2, settings);
        transcriber.sendAudio(2, QByteArray(640, '\0'));
        transcriber.finishInput(2);
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 2000);
        QVERIFY(!readRequest(server.nextPendingConnection()).isEmpty());

        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 2000);
        const QString message = failed.first().first().value<SpeechFailure>().message;
        QVERIFY2(message.contains(QStringLiteral("sent nothing")), qPrintable(message));
        QCOMPARE(completed.size(), 0);
    }

    void speechEndpointCancelledMidFlightEmitsNothing()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        SpeechSettings settings;
        settings.endpoint.baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
        EndpointSpeechTranscriber transcriber;
        QSignalSpy partial(&transcriber, &SpeechTranscriber::partialTranscript);
        QSignalSpy transcript(&transcriber, &SpeechTranscriber::attemptTranscript);
        QSignalSpy completed(&transcriber, &SpeechTranscriber::attemptCompleted);
        QSignalSpy failed(&transcriber, &SpeechTranscriber::failed);

        transcriber.startAttempt(4, settings);
        transcriber.sendAudio(4, QByteArray(640, '\0'));
        transcriber.finishInput(4);
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 2000);
        QTcpSocket *socket = server.nextPendingConnection();
        QVERIFY(!readRequest(socket).isEmpty());
        transcriber.cancelAttempt(4);
        socket->write(httpResponse("200 OK", "application/json", "{\"text\":\"too late\"}"));
        socket->flush();

        QTest::qWait(200);
        QCOMPARE(partial.size() + transcript.size() + completed.size() + failed.size(), 0);
    }

    void chatCompletionsSendsThePromptAndStreamsTheReply()
    {
        FakeServer server;
        server.route("POST /v1/chat/completions",
                     sse({chatChunk(QStringLiteral("Hello")), chatChunk(QStringLiteral(" there."), QStringLiteral("stop")),
                          "[DONE]"}));
        ChatCompletionsRefiner refiner(QStringLiteral("Custom endpoint"), ChatCompletionsRefiner::Audience::Server);
        QSignalSpy completed(&refiner, &ChatCompletionsRefiner::completed);
        QSignalSpy failed(&refiner, &ChatCompletionsRefiner::failed);

        RefinementContext context;
        refiner.refine(QStringLiteral("hello there"), {QStringLiteral("Qt")}, {}, QStringLiteral("key"),
                       server.origin() + QStringLiteral("/v1/"), QStringLiteral("gemma4:e4b"),
                       QStringLiteral("balanced"), context);

        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 2000);
        QCOMPARE(completed.first().first().toString(), QStringLiteral("Hello there."));
        QCOMPARE(failed.size(), 0);
        QVERIFY(server.requests.first().startsWith("POST /v1/chat/completions HTTP/1.1"));
        QVERIFY(headersOf(server.requests.first()).contains("authorization: bearer key"));
        const QJsonObject body = server.jsonBody(0);
        QCOMPARE(body.value(QStringLiteral("model")).toString(), QStringLiteral("gemma4:e4b"));
        QCOMPARE(body.value(QStringLiteral("stream")).toBool(), true);
        QCOMPARE(body.value(QStringLiteral("reasoning_effort")).toString(), QStringLiteral("none"));
        QCOMPARE(body.value(QStringLiteral("chat_template_kwargs")).toObject()
                     .value(QStringLiteral("enable_thinking")), QJsonValue(false));
        const QJsonArray messages = body.value(QStringLiteral("messages")).toArray();
        QCOMPARE(messages.size(), 2);
        QCOMPARE(messages.at(0).toObject().value(QStringLiteral("role")).toString(), QStringLiteral("system"));
        QVERIFY(messages.at(0).toObject().value(QStringLiteral("content")).toString()
                    .startsWith(QStringLiteral("You are Speecher's transcript refinement engine.")));
        QCOMPARE(messages.at(1).toObject().value(QStringLiteral("role")).toString(), QStringLiteral("user"));
        const QString user = messages.at(1).toObject().value(QStringLiteral("content")).toString();
        QVERIFY(user.contains(QStringLiteral("hello there")));
        QVERIFY(user.contains(QStringLiteral("\"preferred_vocabulary\":[\"Qt\"]")));
        QVERIFY(!body.contains(QStringLiteral("temperature")));
    }

    void aSmallLocalModelGetsTheCompactPromptAtTemperatureZero()
    {
        FakeServer server;
        server.route("POST /v1/chat/completions",
                     sse({chatChunk(QStringLiteral("Hello there."), QStringLiteral("stop")), "[DONE]"}));
        ChatCompletionsRefiner refiner(QStringLiteral("Local model"),
                                       ChatCompletionsRefiner::Audience::SmallLocalModel);
        QSignalSpy completed(&refiner, &ChatCompletionsRefiner::completed);
        RefinementContext context;
        refiner.refine(QStringLiteral("um hello there"), {QStringLiteral("Qt")}, {}, {},
                       server.origin() + QStringLiteral("/v1"), QStringLiteral("lfm2.5"),
                       QStringLiteral("balanced"), context);

        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 2000);
        const QJsonObject body = server.jsonBody(0);
        QCOMPARE(body.value(QStringLiteral("temperature")).toDouble(-1), 0.0);
        const QJsonArray messages = body.value(QStringLiteral("messages")).toArray();
        const QString system = messages.at(0).toObject().value(QStringLiteral("content")).toString();
        QVERIFY(system.startsWith(QStringLiteral("You clean up dictated text.")));
        // The full prompt's examples are what a 1B model answered with.
        QVERIFY(!system.contains(QStringLiteral("apple pie")));
        QVERIFY2(system.split(QLatin1Char(' ')).size() < 150, qPrintable(system));
        QVERIFY(system.contains(QStringLiteral("preferred_vocabulary")));
        const QString user = messages.at(1).toObject().value(QStringLiteral("content")).toString();
        QVERIFY(user.contains(QStringLiteral("\"preferred_vocabulary\":[\"Qt\"]")));
    }

    void chatCompletionsRetriesOnceWithoutTheReasoningFields()
    {
        FakeServer server;
        server.route("POST /v1/chat/completions",
                     httpResponse("400 Bad Request", "application/json",
                                  "{\"error\":{\"message\":\"Unrecognized request argument supplied: reasoning_effort\"}}"));
        server.route("POST /v1/chat/completions",
                     sse({chatChunk(QStringLiteral("Fine."), QStringLiteral("stop")), "[DONE]"}));
        ChatCompletionsRefiner refiner(QStringLiteral("Custom endpoint"), ChatCompletionsRefiner::Audience::Server);
        QSignalSpy completed(&refiner, &ChatCompletionsRefiner::completed);
        QSignalSpy failed(&refiner, &ChatCompletionsRefiner::failed);
        RefinementContext context;
        const auto refine = [&] {
            refiner.refine(QStringLiteral("fine"), {}, {}, {}, server.origin() + QStringLiteral("/v1"),
                           QStringLiteral("m"), QStringLiteral("balanced"), context);
        };

        refine();
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 2000);
        QCOMPARE(failed.size(), 0);
        QCOMPARE(server.requests.size(), 2);
        QVERIFY(server.jsonBody(0).contains(QStringLiteral("reasoning_effort")));
        QVERIFY(!server.jsonBody(1).contains(QStringLiteral("reasoning_effort")));
        QVERIFY(!server.jsonBody(1).contains(QStringLiteral("chat_template_kwargs")));

        // The server's refusal is remembered.
        refine();
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 2, 2000);
        QCOMPARE(server.requests.size(), 3);
        QVERIFY(!server.jsonBody(2).contains(QStringLiteral("reasoning_effort")));
    }

    // Text already delivered must not be replayed, even when the error that
    // follows names a reasoning field.
    void chatCompletionsDoesNotRetryAfterStreamedOutput()
    {
        FakeServer server;
        server.route("POST /v1/chat/completions",
                     sse({chatChunk(QStringLiteral("Half")),
                          json({{QStringLiteral("error"), QJsonObject{{QStringLiteral("message"),
                                 QStringLiteral("chat_template_kwargs is not supported")}}}})}));
        ChatCompletionsRefiner refiner(QStringLiteral("Custom endpoint"));
        QSignalSpy delta(&refiner, &ChatCompletionsRefiner::delta);
        QSignalSpy completed(&refiner, &ChatCompletionsRefiner::completed);
        QSignalSpy failed(&refiner, &ChatCompletionsRefiner::failed);
        RefinementContext context;
        refiner.refine(QStringLiteral("half"), {}, {}, {}, server.origin() + QStringLiteral("/v1"),
                       QStringLiteral("m"), QStringLiteral("balanced"), context);

        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 2000);
        QCOMPARE(delta.size(), 1);
        QCOMPARE(completed.size(), 0);
        QTest::qWait(100);
        QCOMPARE(server.requests.size(), 1);
    }

    void messagesFormatFailuresNameTheEndpoint()
    {
        FakeServer server;
        server.route("POST /v1/messages",
                     httpResponse("200 OK", "text/event-stream",
                                  "event: message_delta\ndata: {\"delta\":{\"stop_reason\":\"max_tokens\"}}\n\n"));
        RefinementSettings settings;
        settings.endpoint.format = QStringLiteral("anthropic");
        settings.endpoint.baseUrl = server.origin() + QStringLiteral("/v1");
        settings.endpoint.model = QStringLiteral("m");
        EndpointTranscriptRefiner refiner;
        QSignalSpy failed(&refiner, &TranscriptRefiner::failed);
        RefinementContext context;
        refiner.refine(QStringLiteral("x"), {}, context, settings);

        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 2000);
        const QString message = failed.first().first().toString();
        QVERIFY2(message.startsWith(QStringLiteral("Custom endpoint refinement")), qPrintable(message));
        QVERIFY(!message.contains(QStringLiteral("Anthropic")));
    }

    void chatCompletionsDoesNotRetryOtherErrors()
    {
        FakeServer server;
        server.route("POST /v1/chat/completions",
                     httpResponse("404 Not Found", "application/json",
                                  "{\"error\":\"model \\\"m\\\" not found, try pulling it first\"}"));
        ChatCompletionsRefiner refiner(QStringLiteral("Local model"), ChatCompletionsRefiner::Audience::SmallLocalModel);
        QSignalSpy failed(&refiner, &ChatCompletionsRefiner::failed);
        RefinementContext context;
        refiner.refine(QStringLiteral("x"), {}, {}, {}, server.origin() + QStringLiteral("/v1"),
                       QStringLiteral("m"), QStringLiteral("balanced"), context);

        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 2000);
        QVERIFY2(failed.first().first().toString().contains(QStringLiteral("try pulling it first")),
                 qPrintable(failed.first().first().toString()));
        QTest::qWait(100);
        QCOMPARE(server.requests.size(), 1);
    }

    void anthropicFormatUsesAnApiKeyWithoutClaudeCodeHeaders()
    {
        FakeServer server;
        const QByteArray stream =
            "event: message_start\ndata: {\"type\":\"message_start\"}\n\n"
            "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"delta\":{\"type\":\"text_delta\",\"text\":\"Done.\"}}\n\n"
            "event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n";
        server.route("POST /v1/messages", httpResponse("200 OK", "text/event-stream", stream));
        RefinementSettings settings;
        settings.endpoint.format = QStringLiteral("anthropic");
        settings.endpoint.baseUrl = server.origin() + QStringLiteral("/v1");
        settings.endpoint.model = QStringLiteral("qwen3.5:4b");
        settings.endpoint.apiKey = QStringLiteral("endpoint-key");
        EndpointTranscriptRefiner refiner;
        QSignalSpy completed(&refiner, &TranscriptRefiner::completed);
        RefinementContext context;

        refiner.refine(QStringLiteral("done"), {}, context, settings);

        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 2000);
        QCOMPARE(completed.first().first().toString(), QStringLiteral("Done."));
        const QByteArray headers = headersOf(server.requests.first());
        QVERIFY(headers.contains("x-api-key: endpoint-key"));
        QVERIFY(headers.contains("anthropic-version: 2023-06-01"));
        for (const QByteArray header : {QByteArray("authorization:"), QByteArray("anthropic-beta:"),
                                        QByteArray("x-app:"), QByteArray("claude-cli"),
                                        QByteArray("x-claude-code-session-id:")}) {
            QVERIFY2(!headers.contains(header), header.constData());
        }
        const QJsonObject body = server.jsonBody(0);
        QCOMPARE(body.value(QStringLiteral("model")).toString(), QStringLiteral("qwen3.5:4b"));
        QVERIFY(body.value(QStringLiteral("system")).toString()
                    .startsWith(QStringLiteral("You are Speecher's transcript refinement engine.")));
        QVERIFY(!body.contains(QStringLiteral("thinking")));
        QVERIFY(!body.contains(QStringLiteral("speed")));
    }

    void cliproxyPresetUsesTheCliproxyServerAndKey()
    {
        RefinementSettings settings;
        settings.cliproxyBaseUrl = QStringLiteral("http://proxy.example:8317");
        settings.cliproxyApiKey = QStringLiteral("proxy-key");
        settings.endpoint.preset = QStringLiteral("cliproxy");
        settings.endpoint.baseUrl = QStringLiteral("http://ignored");
        settings.endpoint.apiKey = QStringLiteral("ignored");
        settings.endpoint.model = QStringLiteral("gpt-6-luna");
        const RefinementEndpoint endpoint = resolvedRefinementEndpoint(settings);
        QCOMPARE(endpoint.apiBase, QStringLiteral("http://proxy.example:8317/v1"));
        QCOMPARE(endpoint.apiKey, QStringLiteral("proxy-key"));
        QCOMPARE(endpoint.model, QStringLiteral("gpt-6-luna"));
    }

    void connectionTestListsTheServersModels()
    {
        FakeServer server;
        server.route("GET /v1/models",
                     httpResponse("200 OK", "application/json",
                                  "{\"object\":\"list\",\"data\":[{\"id\":\"llama3.2:3b\"},{\"id\":\"gemma4:e4b\"}]}"));
        RefinementSettings settings;
        settings.endpoint.baseUrl = server.origin() + QStringLiteral("/v1");
        const EndpointCheck check = checkRefinementEndpoint(settings, 2000);
        QVERIFY2(check.ok, qPrintable(check.message));
        QCOMPARE(check.models, (QStringList{QStringLiteral("llama3.2:3b"), QStringLiteral("gemma4:e4b")}));

        SpeechEndpointSettings speech;
        speech.baseUrl = server.origin();
        QCOMPARE(checkSpeechEndpoint(speech, 2000).models.size(), 2);

        settings.endpoint.baseUrl = QStringLiteral("http://127.0.0.1:1/v1");
        QVERIFY(!checkRefinementEndpoint(settings, 2000).ok);
    }

    void ollamaIsIdentifiedByItsOwnBanner()
    {
        FakeServer ollama;
        ollama.route("GET /", httpResponse("200 OK", "text/plain; charset=utf-8", "Ollama is running"));
        ollama.route("GET /api/version", httpResponse("200 OK", "application/json", "{\"version\":\"0.34.4\"}"));
        ollama.route("GET /api/tags", httpResponse("200 OK", "application/json",
                                                   "{\"models\":[{\"name\":\"gemma4:e4b\",\"model\":\"gemma4:e4b\"}]}"));
        const std::optional<DetectedRunner> found = probeOllama(ollama.origin(), 2000);
        QVERIFY(found);
        QCOMPARE(found->id, QStringLiteral("ollama"));
        QCOMPARE(found->version, QStringLiteral("0.34.4"));
        QCOMPARE(found->apiBase, ollama.origin() + QStringLiteral("/v1"));
        QCOMPARE(found->models, QStringList{QStringLiteral("gemma4:e4b")});

        // KoboldCpp answers Ollama's API routes with a fixed version, but
        // serves its own page at the root.
        FakeServer kobold;
        kobold.route("GET /", httpResponse("200 OK", "text/html", "<html>KoboldAI Lite</html>"));
        kobold.route("GET /api/version", httpResponse("200 OK", "application/json", "{\"version\":\"0.7.0\"}"));
        kobold.route("GET /api/tags", httpResponse("200 OK", "application/json",
                                                   "{\"models\":[{\"name\":\"koboldcpp/model\"}]}"));
        kobold.route("GET /props", httpResponse("200 OK", "application/json",
                                                "{\"model_path\":\"model\",\"total_slots\":1}"));
        kobold.route("GET /health", httpResponse("200 OK", "application/json", "{\"status\":\"ok\"}"));
        QVERIFY(!probeOllama(kobold.origin(), 2000));
        QVERIFY(!probeLlamaServer(kobold.origin(), 2000));
        QVERIFY(!probeLmStudio(kobold.origin(), 2000));
    }

    void llamaServerLoadingItsModelIsFound()
    {
        FakeServer llama;
        llama.route("GET /health", httpResponse("503 Service Unavailable", "application/json",
                                                "{\"error\":{\"message\":\"Loading model\","
                                                "\"type\":\"unavailable_error\",\"code\":503}}"));
        const std::optional<DetectedRunner> loading = probeLlamaServer(llama.origin(), 2000);
        QVERIFY(loading);
        QCOMPARE(loading->id, QStringLiteral("llama-server"));
        QVERIFY(loading->models.isEmpty());

        // whisper.cpp's server loading its model is not llama-server.
        FakeServer whisper;
        whisper.route("GET /health", httpResponse("503 Service Unavailable", "application/json",
                                                  "{\"status\":\"loading model\"}"));
        QVERIFY(!probeLlamaServer(whisper.origin(), 2000));
    }

    void lmStudioBefore04IsFoundThroughItsV0Listing()
    {
        FakeServer lmStudio;
        lmStudio.route("GET /api/v0/models",
                       httpResponse("200 OK", "application/json",
                                    "{\"object\":\"list\",\"data\":["
                                    "{\"id\":\"qwen2-vl-7b-instruct\",\"object\":\"model\",\"type\":\"vlm\","
                                    "\"compatibility_type\":\"mlx\",\"state\":\"not-loaded\"},"
                                    "{\"id\":\"text-embedding-nomic\",\"object\":\"model\",\"type\":\"embeddings\","
                                    "\"compatibility_type\":\"gguf\"}]}"));
        const std::optional<DetectedRunner> studio = probeLmStudio(lmStudio.origin(), 2000);
        QVERIFY(studio);
        QCOMPARE(studio->models, QStringList{QStringLiteral("qwen2-vl-7b-instruct")});
    }

    void llamaServerAndLmStudioAreIdentified()
    {
        FakeServer llama;
        llama.route("GET /health", httpResponse("200 OK", "application/json", "{\"status\":\"ok\"}"));
        llama.route("GET /props", httpResponse("200 OK", "application/json",
                                               "{\"model_path\":\"/models/LFM2.5-1.2B-Instruct-Q4_K_M.gguf\","
                                               "\"build_info\":\"b11185-abc1234\"}"));
        const std::optional<DetectedRunner> server = probeLlamaServer(llama.origin(), 2000);
        QVERIFY(server);
        QCOMPARE(server->version, QStringLiteral("b11185-abc1234"));
        QCOMPARE(server->models, QStringList{QStringLiteral("LFM2.5-1.2B-Instruct-Q4_K_M.gguf")});
        QVERIFY(!probeOllama(llama.origin(), 2000));

        FakeServer lmStudio;
        lmStudio.route("GET /api/v1/models",
                       httpResponse("200 OK", "application/json",
                                    "{\"models\":[{\"type\":\"llm\",\"key\":\"google/gemma-4-e4b\"},"
                                    "{\"type\":\"embedding\",\"key\":\"nomic-embed\"}]}"));
        const std::optional<DetectedRunner> studio = probeLmStudio(lmStudio.origin(), 2000);
        QVERIFY(studio);
        QCOMPARE(studio->models, QStringList{QStringLiteral("google/gemma-4-e4b")});
    }

    void cleanupSuggestionFollowsTheLatencyTable()
    {
        QCOMPARE(suggestedCleanupModel(CleanupHardware::DedicatedGpu)->ollamaTag, QStringLiteral("gemma4:e4b"));
        QCOMPARE(suggestedCleanupModel(CleanupHardware::AppleMax)->ollamaTag, QStringLiteral("gemma4:e4b"));
        QCOMPARE(suggestedCleanupModel(CleanupHardware::IntegratedGpu)->ollamaTag,
                 QStringLiteral("LiquidAI/lfm2.5-1.2b-instruct"));
        QCOMPARE(suggestedCleanupModel(CleanupHardware::Cpu)->ollamaTag,
                 QStringLiteral("LiquidAI/lfm2.5-1.2b-instruct"));
        QCOMPARE(suggestedCleanupModel(CleanupHardware::ApplePro)->ollamaTag,
                 QStringLiteral("LiquidAI/lfm2.5-1.2b-instruct"));
    }

    void hardwareMapsToTheLatencyTableRow()
    {
        constexpr quint64 gib = quint64(1) << 30;
        const auto row = [](HardwareProfile::Accelerator accelerator, const QString &chip, quint64 gpu) {
            HardwareProfile hardware;
            hardware.accelerator = accelerator;
            hardware.chipName = chip;
            hardware.gpuMemoryBytes = gpu;
            return cleanupHardwareFor(hardware);
        };
        using A = HardwareProfile::Accelerator;
        QCOMPARE(row(A::Cpu, QStringLiteral("AMD Ryzen 9 7945HX"), 0), CleanupHardware::Cpu);
        QCOMPARE(row(A::IntegratedGpu, QStringLiteral("AMD Ryzen 7 PRO 4750U"), 0),
                 CleanupHardware::IntegratedGpu);
        QCOMPARE(row(A::DedicatedGpu, QString(), 12 * gib), CleanupHardware::DedicatedGpu);
        // Gemma 4 E4B does not fit a 6 GB card and would run on the processor.
        QCOMPARE(row(A::DedicatedGpu, QString(), 6 * gib), CleanupHardware::Cpu);
        QCOMPARE(row(A::AppleSilicon, QStringLiteral("Apple M1"), 8 * gib), CleanupHardware::AppleBase);
        QCOMPARE(row(A::AppleSilicon, QStringLiteral("Apple M3 Pro"), 18 * gib), CleanupHardware::ApplePro);
        QCOMPARE(row(A::AppleSilicon, QStringLiteral("Apple M4 Max"), 36 * gib), CleanupHardware::AppleMax);
        QCOMPARE(row(A::AppleSilicon, QStringLiteral("Apple M2 Ultra"), 64 * gib), CleanupHardware::AppleMax);
    }

    void ollamaPullReportsProgressAcrossLayers()
    {
        FakeServer ollama;
        const QByteArray lines =
            "{\"status\":\"pulling manifest\"}\n"
            "{\"status\":\"pulling a\",\"digest\":\"sha256:a\",\"total\":100,\"completed\":50}\n"
            "{\"status\":\"pulling b\",\"digest\":\"sha256:b\",\"total\":300,\"completed\":300}\n"
            "{\"status\":\"pulling a\",\"digest\":\"sha256:a\",\"total\":100,\"completed\":100}\n"
            "{\"status\":\"success\"}\n";
        ollama.route("POST /api/pull", httpResponse("200 OK", "application/x-ndjson", lines));
        OllamaPull pull;
        QSignalSpy progress(&pull, &OllamaPull::progress);
        QSignalSpy finished(&pull, &OllamaPull::finished);
        QSignalSpy failed(&pull, &OllamaPull::failed);

        pull.start(QStringLiteral("gemma4:e4b"), ollama.origin());

        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 2000);
        QCOMPARE(failed.size(), 0);
        QCOMPARE(ollama.jsonBody(0).value(QStringLiteral("model")).toString(), QStringLiteral("gemma4:e4b"));
        QCOMPARE(progress.at(2).at(0).toLongLong(), 350);
        QCOMPARE(progress.at(2).at(1).toLongLong(), 400);
        QCOMPARE(progress.at(3).at(0).toLongLong(), 400);

        FakeServer missing;
        missing.route("POST /api/pull", httpResponse("200 OK", "application/x-ndjson",
                                                     "{\"status\":\"pulling manifest\"}\n"
                                                     "{\"error\":\"pull model manifest: file does not exist\"}\n"));
        pull.start(QStringLiteral("nope"), missing.origin());
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 2000);
        QVERIFY(failed.first().first().toString().contains(QStringLiteral("file does not exist")));
    }

    void endpointSettingsRoundTrip()
    {
        SettingsStore settings;
        settings.raw().clear();
        AppSettings draft = settings.snapshot();
        QCOMPARE(draft.speech.endpoint, SpeechEndpointSettings{});
        QCOMPARE(draft.refinement.endpoint, RefinementEndpointSettings{});
        QCOMPARE(draft.refinement.localRunner, LocalRunnerSettings{});

        draft.speech.providerId = QStringLiteral("endpoint");
        draft.speech.endpoint = {QStringLiteral(" http://whisper.local:8080/ "), QStringLiteral("inference"),
                                 QStringLiteral("whisper-1"), {}};
        draft.refinement.providerId = QStringLiteral("endpoint");
        draft.refinement.endpoint = {QStringLiteral("cliproxy"), QStringLiteral("anthropic"),
                                     QStringLiteral("http://localhost:11434/v1/"), QStringLiteral("gemma4:e4b"), {}};
        draft.refinement.localRunner = {QStringLiteral("llama-server"), QStringLiteral("lfm")};
        settings.applySnapshot(draft);

        const AppSettings loaded = settings.snapshot();
        QCOMPARE(loaded.speech.providerId, QStringLiteral("endpoint"));
        QCOMPARE(loaded.speech.endpoint.baseUrl, QStringLiteral("http://whisper.local:8080"));
        QCOMPARE(loaded.speech.endpoint.path, QStringLiteral("/inference"));
        QCOMPARE(loaded.speech.endpoint.model, QStringLiteral("whisper-1"));
        QCOMPARE(loaded.refinement.providerId, QStringLiteral("endpoint"));
        QCOMPARE(loaded.refinement.endpoint.preset, QStringLiteral("cliproxy"));
        QCOMPARE(loaded.refinement.endpoint.format, QStringLiteral("anthropic"));
        QCOMPARE(loaded.refinement.endpoint.baseUrl, QStringLiteral("http://localhost:11434/v1"));
        QCOMPARE(loaded.refinement.endpoint.model, QStringLiteral("gemma4:e4b"));
        QCOMPARE(loaded.refinement.localRunner.runner, QStringLiteral("llama-server"));
        QCOMPARE(loaded.refinement.localRunner.model, QStringLiteral("lfm"));

        settings.setRefinementProvider(QStringLiteral("local"));
        QCOMPARE(settings.refinementProvider(), QStringLiteral("local"));
        settings.raw().setValue(SettingsKeys::RefinementEndpointFormat, QStringLiteral("bogus"));
        QCOMPARE(settings.snapshot().refinement.endpoint.format, QStringLiteral("openai"));
    }

    // Keys land in the keyring when one works, and in the settings file
    // otherwise; either way they read back and never double up.
    void endpointKeysRoundTripOutsideTheSettingsFile()
    {
        SettingsStore settings;
        settings.raw().clear();
        AppSettings draft = settings.snapshot();
        draft.speech.endpoint.apiKey = QStringLiteral("speech-secret");
        draft.refinement.endpoint.apiKey = QStringLiteral("refinement-secret");
        settings.applySnapshot(draft);

        SettingsStore reopened;
        const AppSettings loaded = reopened.snapshot();
        QCOMPARE(loaded.speech.endpoint.apiKey, QStringLiteral("speech-secret"));
        QCOMPARE(loaded.refinement.endpoint.apiKey, QStringLiteral("refinement-secret"));
        const bool inKeyring = reopened.raw().value(SettingsKeys::SecretsInKeyring).toStringList()
                                   .contains(QStringLiteral("speech-endpoint-key"));
        QCOMPARE(reopened.raw().contains(SettingsKeys::SpeechEndpointApiKey), !inKeyring);
        qInfo() << "endpoint keys stored in" << (inKeyring ? "the keyring" : "the settings file");

        draft = loaded;
        draft.speech.endpoint.apiKey.clear();
        draft.refinement.endpoint.apiKey.clear();
        reopened.applySnapshot(draft);
        QCOMPARE(SettingsStore().snapshot().speech.endpoint.apiKey, QString());
        QVERIFY(!reopened.raw().contains(SettingsKeys::SpeechEndpointApiKey));
        QVERIFY(!reopened.raw().contains(SettingsKeys::SecretsInKeyring));
    }

    // A keyring that does not answer must not make a migrated CLI Proxy API
    // key look unset, and a snapshot saved meanwhile must not erase it.
    void keyringTimeoutKeepsTheMigratedCliproxyKey()
    {
        {
            SettingsStore settings;
            settings.raw().clear();
            settings.setOpenAiAuthMode(QStringLiteral("cliproxy"));
            settings.setCliproxyBaseUrl(QStringLiteral("http://proxy.example:8317"));
            // As SecretStore records a key it moved into the keyring.
            settings.raw().setValue(SettingsKeys::SecretsInKeyring, QStringList{QStringLiteral("cliproxy-api-key")});
            settings.raw().sync();
        }
        qputenv("SPEECHER_TEST_KEYRING_READ_TIMEOUT", "1");
        const auto restore = qScopeGuard([] { qunsetenv("SPEECHER_TEST_KEYRING_READ_TIMEOUT"); });
        SettingsStore settings;
        AppSettings draft = settings.snapshot();
        QCOMPARE(draft.refinement.cliproxyApiKey, QString());

        draft.refinement.cliproxyBaseUrl = QStringLiteral("http://proxy.example:8318");
        settings.applySnapshot(draft);
        QCOMPARE(settings.raw().value(SettingsKeys::SecretsInKeyring).toStringList(),
                 QStringList{QStringLiteral("cliproxy-api-key")});
        QVERIFY(!settings.raw().contains(SettingsKeys::CliproxyApiKey));
    }

    // Existing CLI Proxy API users keep their key: it moves from the settings
    // file to the keyring on first load when a keyring accepts it.
    void cliproxyKeyMigratesOutOfTheSettingsFile()
    {
        {
            SettingsStore settings;
            settings.raw().clear();
            settings.raw().setValue(SettingsKeys::CliproxyBaseUrl, QStringLiteral("http://proxy.example:8317"));
            settings.raw().setValue(SettingsKeys::CliproxyApiKey, QStringLiteral(" legacy-proxy-key "));
            settings.raw().sync();
        }
        SettingsStore settings;
        QCOMPARE(settings.cliproxyApiKey(), QStringLiteral("legacy-proxy-key"));
        QCOMPARE(settings.snapshot().refinement.cliproxyApiKey, QStringLiteral("legacy-proxy-key"));
        const bool migrated = settings.raw().value(SettingsKeys::SecretsInKeyring).toStringList()
                                  .contains(QStringLiteral("cliproxy-api-key"));
        QCOMPARE(settings.raw().contains(SettingsKeys::CliproxyApiKey), !migrated);
        qInfo() << "cliproxy key migrated to the keyring:" << migrated;

        settings.setCliproxyApiKey(QString());
        QCOMPARE(SettingsStore().cliproxyApiKey(), QString());
    }
};

int runCustomEndpointsTests(int argc, char **argv)
{
    CustomEndpointsTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_custom_endpoints.moc"
