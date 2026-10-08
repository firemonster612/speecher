#include "common/test_prelude.h"
#include "common/test_http.h"
#include "common/test_doubles.h"
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

#include <optional>

using namespace speecher::test;

namespace {

QByteArray json(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

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

void dictate(EndpointSpeechTranscriber &transcriber, const QString &origin, quint64 attempt = 1,
             const QStringList &vocabulary = {})
{
    SpeechSettings settings;
    settings.endpoint.baseUrl = origin;
    settings.vocabulary = vocabulary;
    transcriber.startAttempt(attempt, settings);
    transcriber.sendAudio(attempt, QByteArray(640, '\0'));
    transcriber.finishInput(attempt);
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
        endpoint.path = QStringLiteral("/inference");
        endpoint.model = QStringLiteral("whisper-large-v3-turbo");
        endpoint.apiKey = QStringLiteral("speech-key");
        const QByteArray pcm(3200, '\x01');

        FakeServer server;
        server.route("POST /inference", httpResponse("200 OK", "application/json", "{\"text\":\"ok\"}"));
        endpoint.baseUrl = server.origin();
        const SpeechEndpointUpload upload = speechEndpointUpload(endpoint, pcm, {}, QStringLiteral("de"));
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
                                       QByteArray("language\"\r\n\r\nde"),
                                       QByteArray("stream\"\r\n\r\ntrue")}) {
            QVERIFY2(body.contains(field), field.constData());
        }
        QVERIFY(!body.contains("name=\"prompt\""));
    }

    void speechUploadCarriesTheKeyTermsAsItsPrompt()
    {
        FakeServer server;
        server.route("POST /v1/audio/transcriptions", httpResponse("200 OK", "application/json", "{\"text\":\"ok\"}"));
        EndpointSpeechTranscriber transcriber;
        QSignalSpy completed(&transcriber, &SpeechTranscriber::attemptCompleted);
        dictate(transcriber, server.origin(), 1, {QStringLiteral("Speecher"), QStringLiteral("Kirigami Addons")});
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 2000);
        QCOMPARE(server.requests.size(), 1);
        QVERIFY(server.requests.first().contains("name=\"prompt\"\r\n\r\nSpeecher, Kirigami Addons\r\n"));
    }

    void speechUploadLeavesTheLanguageToTheServerForAutomatic()
    {
        FakeServer server;
        server.route("POST /v1/audio/transcriptions", httpResponse("200 OK", "application/json", "{\"text\":\"ok\"}"));
        EndpointSpeechTranscriber transcriber;
        QSignalSpy completed(&transcriber, &SpeechTranscriber::attemptCompleted);
        SpeechSettings settings;
        settings.endpoint.baseUrl = server.origin();
        settings.language = QStringLiteral("auto");
        transcriber.startAttempt(1, settings);
        transcriber.sendAudio(1, QByteArray(640, '\0'));
        transcriber.finishInput(1);
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 2000);
        QVERIFY(!server.requests.first().contains("name=\"language\""));
    }

    // The endpoint gets what Claude Voice gets: 101 short terms send 100.
    void speechPromptUsesTheKeyTermLimits()
    {
        QStringList terms;
        for (int index = 1; index <= 101; ++index) {
            terms << QStringLiteral("term%1").arg(index, 4, 10, QLatin1Char('0'));
        }
        FakeServer server;
        server.route("POST /v1/audio/transcriptions", httpResponse("200 OK", "application/json", "{\"text\":\"ok\"}"));
        EndpointSpeechTranscriber transcriber;
        QSignalSpy completed(&transcriber, &SpeechTranscriber::attemptCompleted);
        dictate(transcriber, server.origin(), 1, terms);
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 2000);
        const QByteArray request = server.requests.first();
        QVERIFY(request.contains("term0001, term0002, "));
        QVERIFY(request.contains(", term0100\r\n"));
        QVERIFY(!request.contains("term0101"));
    }

    void endpointsRejectCrossOriginRedirects()
    {
        FakeServer destination;
        FakeServer server;
        const QByteArray redirect = "HTTP/1.1 307 Temporary Redirect\r\nLocation: " + destination.origin().toUtf8()
            + "/stolen\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        server.route("POST /v1/audio/transcriptions", redirect);
        server.route("GET /v1/models", redirect);
        EndpointSpeechTranscriber transcriber;
        QSignalSpy failed(&transcriber, &SpeechTranscriber::failed);
        QSignalSpy completed(&transcriber, &SpeechTranscriber::attemptCompleted);
        dictate(transcriber, server.origin());
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 2000);
        QCOMPARE(completed.size(), 0);
        const auto failure = failed.first().first().value<SpeechFailure>();
        QVERIFY2(failure.message.contains(QStringLiteral("redirect"), Qt::CaseInsensitive), qPrintable(failure.message));

        SpeechEndpointSettings endpoint;
        endpoint.baseUrl = server.origin();
        const auto check = checkSpeechEndpoint(endpoint);
        QVERIFY(!check.ok);
        QVERIFY2(check.message.contains(QStringLiteral("redirect"), Qt::CaseInsensitive), qPrintable(check.message));
        QCOMPARE(server.requests.size(), 2);
        QCOMPARE(destination.requests.size(), 0);
    }

    void speechEndpointReadsAJsonTranscriptOnce()
    {
        FakeServer server;
        server.route("POST /v1/audio/transcriptions",
                     httpResponse("200 OK", "application/json", "{\"text\":\" Hello there. \"}"));
        EndpointSpeechTranscriber transcriber;
        QSignalSpy transcript(&transcriber, &SpeechTranscriber::attemptTranscript);
        QSignalSpy completed(&transcriber, &SpeechTranscriber::attemptCompleted);

        dictate(transcriber, server.origin(), 7);

        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 2000);
        QCOMPARE(transcript.size(), 1);
        QCOMPARE(transcript.first().at(0).toULongLong(), 7ull);
        QCOMPARE(transcript.first().at(1).toString(), QStringLiteral("Hello there."));
        QCOMPARE(server.requests.size(), 1);
    }

    void speechEndpointReadsAStreamedTranscript_data()
    {
        QTest::addColumn<QByteArray>("stream");
        QTest::addColumn<QStringList>("partials");
        QTest::addColumn<QString>("text");
        QTest::newRow("openai")
            << QByteArray("event: transcript.text.delta\ndata: {\"type\":\"transcript.text.delta\",\"delta\":\"Hello\"}\n\n"
                          "data: {\"type\":\"transcript.text.delta\",\"delta\":\" world\"}\n\n"
                          "data: {\"type\":\"transcript.text.done\",\"text\":\"Hello, world.\"}\n\n")
            << QStringList{QStringLiteral("Hello"), QStringLiteral("Hello world")} << QStringLiteral("Hello, world.");
        // Speaches 0.9 sends one delta per segment and an empty done text.
        QTest::newRow("speaches-0.9")
            << QByteArray("data: {\"type\":\"transcript.text.delta\",\"delta\":\" Hello there.\",\"logprobs\":null}\n\n"
                          "data: {\"type\":\"transcript.text.delta\",\"delta\":\" How are you?\",\"logprobs\":null}\n\n"
                          "data: {\"type\":\"transcript.text.done\",\"text\":\"\",\"logprobs\":null}\n\n")
            << QStringList{QStringLiteral(" Hello there."), QStringLiteral(" Hello there. How are you?")}
            << QStringLiteral("Hello there. How are you?");
        // Speaches 0.8 sends each segment trimmed, as an untyped json transcription.
        QTest::newRow("speaches-0.8")
            << QByteArray("data: {\"text\":\"Hello there.\"}\n\ndata: {\"text\":\"How are you?\"}\n\n")
            << QStringList{QStringLiteral("Hello there."), QStringLiteral("Hello there. How are you?")}
            << QStringLiteral("Hello there. How are you?");
        QTest::newRow("speaches-0.8-chinese")
            << QByteArray("data: {\"text\":\"你好。\"}\n\ndata: {\"text\":\"2026年开始。\"}\n\n")
            << QStringList{QStringLiteral("你好。"), QStringLiteral("你好。2026年开始。")}
            << QStringLiteral("你好。2026年开始。");
    }

    void speechEndpointReadsAStreamedTranscript()
    {
        QFETCH(QByteArray, stream);
        QFETCH(QStringList, partials);
        QFETCH(QString, text);
        FakeServer server;
        server.route("POST /v1/audio/transcriptions", httpResponse("200 OK", "text/event-stream", stream));
        EndpointSpeechTranscriber transcriber;
        QSignalSpy partial(&transcriber, &SpeechTranscriber::partialTranscript);
        QSignalSpy transcript(&transcriber, &SpeechTranscriber::attemptTranscript);
        QSignalSpy completed(&transcriber, &SpeechTranscriber::attemptCompleted);

        dictate(transcriber, server.origin());

        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 2000);
        QStringList streamed;
        for (const QList<QVariant> &signal : partial) streamed << signal.at(1).toString();
        QCOMPARE(streamed, partials);
        QCOMPARE(transcript.size(), 1);
        QCOMPARE(transcript.first().at(1).toString(), text);
    }

    // Rule A7: a failed upload is not sent again, and text the stream already
    // produced is kept as the dictation.
    void speechEndpointFailuresKeepStreamedTextAndAreNotRetried_data()
    {
        const QByteArray keepThis = "data: {\"type\":\"transcript.text.delta\",\"delta\":\"Keep this\"}\n\n";
        const QByteArray sseError = "event: error\ndata: {\"type\":\"error\",\"message\":\"model failed\"}\n\n";
        QTest::addColumn<QByteArray>("response");
        QTest::addColumn<QString>("message");
        QTest::addColumn<QString>("kept");
        QTest::addColumn<ProviderFailureKind>("kind");
        QTest::newRow("http-500") << httpResponse("500 Internal Server Error", "application/json",
                                                  "{\"error\":{\"message\":\"model not loaded\"}}")
                                  << "model not loaded" << QString() << ProviderFailureKind::Server;
        QTest::newRow("http-401") << httpResponse("401 Unauthorized", "application/json",
                                                  "{\"error\":{\"message\":\"bad key\"}}")
                                  << "bad key" << QString() << ProviderFailureKind::Authentication;
        QTest::newRow("http-429") << httpResponse("429 Too Many Requests", "application/json",
                                                  "{\"error\":{\"message\":\"busy\"}}")
                                  << "busy" << QString() << ProviderFailureKind::RateLimited;
        QTest::newRow("sse-error-before-output")
            << httpResponse("200 OK", "text/event-stream", sseError) << "model failed" << QString()
            << ProviderFailureKind::Server;
        QTest::newRow("sse-error-after-output")
            << httpResponse("200 OK", "text/event-stream", keepThis + sseError) << "model failed" << "Keep this"
            << ProviderFailureKind::Server;
        // The specific code outranks the generic type.
        QTest::newRow("sse-code-names-the-key")
            << httpResponse("200 OK", "text/event-stream",
                            "event: error\ndata: {\"type\":\"error\",\"error\":{\"type\":\"invalid_request_error\","
                            "\"code\":\"invalid_api_key\",\"message\":\"bad key\"}}\n\n")
            << "bad key" << QString() << ProviderFailureKind::Authentication;
        QTest::newRow("sse-code-names-the-rate-limit")
            << httpResponse("200 OK", "text/event-stream",
                            "event: error\ndata: {\"type\":\"error\",\"error\":{\"type\":\"invalid_request_error\","
                            "\"code\":\"rate_limit_exceeded\",\"message\":\"slow down\"}}\n\n")
            << "slow down" << QString() << ProviderFailureKind::RateLimited;
        QTest::newRow("dropped-stream") << "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
                                           "Content-Length: 10000\r\nConnection: close\r\n\r\n" + keepThis
                                        << QString() << "Keep this" << ProviderFailureKind::Network;
    }

    void speechEndpointFailuresKeepStreamedTextAndAreNotRetried()
    {
        QFETCH(QByteArray, response);
        QFETCH(QString, message);
        QFETCH(QString, kept);
        QFETCH(ProviderFailureKind, kind);
        FakeServer server;
        server.route("POST /v1/audio/transcriptions", response);
        EndpointSpeechTranscriber transcriber;
        QSignalSpy failed(&transcriber, &SpeechTranscriber::failed);
        QSignalSpy transcript(&transcriber, &SpeechTranscriber::attemptTranscript);
        QSignalSpy completed(&transcriber, &SpeechTranscriber::attemptCompleted);
        dictate(transcriber, server.origin());
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 2000);
        const SpeechFailure failure = failed.first().first().value<SpeechFailure>();
        QVERIFY2(failure.message.contains(message), qPrintable(failure.message));
        QCOMPARE(failure.kind, kind);
        QCOMPARE(failure.phase, QStringLiteral("finalize"));
        QVERIFY(!failure.retryable);
        QCOMPARE(transcript.size(), kept.isEmpty() ? 0 : 1);
        if (!kept.isEmpty()) QCOMPARE(transcript.first().at(1).toString(), kept);
        QTest::qWait(100);
        QCOMPARE(completed.size(), 0);
        QCOMPARE(server.requests.size(), 1);
    }

    // The server keeps the connection busy past the inactivity limit, then
    // goes quiet: the streamed text so far is the dictation.
    void speechEndpointKeepsStreamingPastTheInactivityLimitAndKeepsTextWhenItStalls()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        EndpointSpeechTranscriber transcriber(nullptr, 300, 5000);
        QSignalSpy partial(&transcriber, &SpeechTranscriber::partialTranscript);
        QSignalSpy transcript(&transcriber, &SpeechTranscriber::attemptTranscript);
        QSignalSpy completed(&transcriber, &SpeechTranscriber::attemptCompleted);
        QSignalSpy failed(&transcriber, &SpeechTranscriber::failed);

        dictate(transcriber, QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 2000);
        QTcpSocket *socket = server.nextPendingConnection();
        QTRY_VERIFY_WITH_TIMEOUT(hasWholeRequest(socket), 2000);
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n\r\n");
        for (const char *word : {"One", " two", " three", " four"}) {
            socket->write("data: {\"type\":\"transcript.text.delta\",\"delta\":\"" + QByteArray(word) + "\"}\n\n");
            socket->flush();
            QTest::qWait(150);
        }
        QCOMPARE(partial.last().at(1).toString(), QStringLiteral("One two three four"));
        QCOMPARE(failed.size(), 0);
        QCOMPARE(completed.size(), 0);

        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 2000);
        QCOMPARE(completed.size(), 0);
        const auto failure = failed.first().first().value<SpeechFailure>();
        QCOMPARE(failure.phase, QStringLiteral("finalize"));
        QVERIFY(failure.message.contains(QStringLiteral("sent nothing")));
        QCOMPARE(failure.kind, ProviderFailureKind::Timeout);
        QVERIFY(!failure.retryable);
        QVERIFY(!server.hasPendingConnections());
        QCOMPARE(transcript.size(), 1);
        QCOMPARE(transcript.first().at(1).toString(), QStringLiteral("One two three four"));
    }

    void speechEndpointCancelledMidFlightEmitsNothing()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        EndpointSpeechTranscriber transcriber;
        QSignalSpy partial(&transcriber, &SpeechTranscriber::partialTranscript);
        QSignalSpy transcript(&transcriber, &SpeechTranscriber::attemptTranscript);
        QSignalSpy completed(&transcriber, &SpeechTranscriber::attemptCompleted);
        QSignalSpy failed(&transcriber, &SpeechTranscriber::failed);

        dictate(transcriber, QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()), 4);
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 2000);
        QTcpSocket *socket = server.nextPendingConnection();
        QTRY_VERIFY_WITH_TIMEOUT(hasWholeRequest(socket), 2000);
        transcriber.cancelAttempt(4);
        socket->write(httpResponse("200 OK", "application/json", "{\"text\":\"too late\"}"));
        socket->flush();

        QTest::qWait(200);
        QCOMPARE(partial.size() + transcript.size() + completed.size() + failed.size(), 0);
    }

    // A recording's utterances go up one at a time, each once the one before
    // has answered, with the key terms and then the last 200 characters of
    // the text before it, from a word's start, as its prompt. Each text is a
    // final, and the quiet outside the utterances is not sent.
    void speechEndpointTranscribesEachUtteranceInTurnAfterTheTextBefore()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        EndpointSpeechTranscriber transcriber;
        QSignalSpy finals(&transcriber, &SpeechTranscriber::finalTranscript);
        QSignalSpy transcript(&transcriber, &SpeechTranscriber::attemptTranscript);
        QSignalSpy completed(&transcriber, &SpeechTranscriber::attemptCompleted);
        SpeechSettings settings;
        settings.endpoint.baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
        settings.vocabulary = {QStringLiteral("Speecher")};
        settings.cutIntoUtterances = true;
        transcriber.startAttempt(1, settings);
        transcriber.beginUtterance(1);
        transcriber.sendAudio(1, QByteArray(640, 'a'));
        transcriber.endUtterance(1);
        transcriber.beginUtterance(1);
        transcriber.sendAudio(1, QByteArray(640, 'b'));
        transcriber.endUtterance(1);
        transcriber.sendAudio(1, QByteArray(640, 'c'));
        transcriber.finishInput(1);

        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 2000);
        QTcpSocket *first = server.nextPendingConnection();
        const QByteArray firstRequest = readHttpRequest(first, 2000);
        QVERIFY(firstRequest.contains(QByteArray(640, 'a')));
        QVERIFY(!firstRequest.contains(QByteArray(640, 'b')));
        QVERIFY(firstRequest.contains("name=\"prompt\"\r\n\r\nSpeecher\r\n"));
        QTest::qWait(100);
        QVERIFY(!server.hasPendingConnections());
        // 282 characters: the tail starts 200 from the end, part way into a
        // "word", so at the next.
        const QString firstText = QStringLiteral("word ").repeated(50) + QStringLiteral("Can you look at the retry logic?");
        first->write(httpResponse("200 OK", "application/json", json({{QStringLiteral("text"), firstText}})));
        first->flush();

        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 2000);
        QTcpSocket *second = server.nextPendingConnection();
        const QByteArray secondRequest = readHttpRequest(second, 2000);
        QVERIFY(secondRequest.contains(QByteArray(640, 'b')));
        const QByteArray prompt = "name=\"prompt\"\r\n\r\nSpeecher. " + QByteArray("word ").repeated(33)
            + "Can you look at the retry logic?\r\n";
        QVERIFY2(secondRequest.contains(prompt), secondRequest.constData());
        second->write(httpResponse("200 OK", "application/json", "{\"text\":\" It drops the last chunk. \"}"));
        second->flush();

        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 2000);
        QCOMPARE(finals.size(), 2);
        QCOMPARE(finals.at(0).at(1).toString(), firstText);
        QCOMPARE(finals.at(1).at(1).toString(), QStringLiteral("It drops the last chunk."));
        QCOMPARE(transcript.size(), 0);
        QTest::qWait(100);
        QVERIFY(!server.hasPendingConnections());
    }

    // An utterance the server hears no words in is still answered: its final
    // is empty.
    void speechEndpointAnswersAnUtteranceWithNoWordsWithAnEmptyFinal()
    {
        FakeServer server;
        server.route("POST /v1/audio/transcriptions", httpResponse("200 OK", "application/json", "{\"text\":\" \"}"));
        EndpointSpeechTranscriber transcriber;
        QSignalSpy finals(&transcriber, &SpeechTranscriber::finalTranscript);
        SpeechSettings settings;
        settings.endpoint.baseUrl = server.origin();
        settings.cutIntoUtterances = true;
        transcriber.startAttempt(1, settings);
        transcriber.beginUtterance(1);
        transcriber.sendAudio(1, QByteArray(640, 'a'));
        transcriber.endUtterance(1);

        QTRY_COMPARE_WITH_TIMEOUT(finals.size(), 1, 2000);
        QCOMPARE(finals.first().at(1).toString(), QString());
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
        // More terms than speech takes: a small model gets only the speech cap.
        QStringList vocabulary{QStringLiteral("Qt")};
        for (int index = 0; index < 150; ++index) {
            vocabulary << QStringLiteral("term%1").arg(index);
        }
        refiner.refine(QStringLiteral("um hello there"), vocabulary, {}, {},
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
        QVERIFY(user.contains(QStringLiteral("\"preferred_vocabulary\":[\"Qt\",\"term0\",")));
        QVERIFY(user.contains(QStringLiteral("\"term98\"]")));
        QVERIFY(!user.contains(QStringLiteral("\"term99\"")));
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

    // Only an HTTP 400 naming a reasoning field before any output drops the
    // fields; anything else fails once and the next request keeps them.
    void reasoningFieldErrorsOnlyRetryHttp400_data()
    {
        QTest::addColumn<QByteArray>("response");
        QTest::addColumn<QString>("message");
        QTest::addColumn<ProviderFailureKind>("kind");
        QTest::newRow("http-500") << httpResponse("500 Internal Server Error", "application/json",
            "{\"error\":{\"message\":\"reasoning_effort crashed\"}}") << "reasoning_effort crashed"
            << ProviderFailureKind::Server;
        QTest::newRow("sse-200") << sse({"{\"error\":{\"message\":\"reasoning_effort crashed\"}}"})
                                 << "reasoning_effort crashed" << ProviderFailureKind::Server;
        // The specific code outranks the generic type.
        QTest::newRow("sse-code-names-the-key") << sse({"{\"error\":{\"type\":\"invalid_request_error\","
                                                        "\"code\":\"invalid_api_key\",\"message\":\"bad key\"}}"})
                                                << "bad key" << ProviderFailureKind::Authentication;
        QTest::newRow("sse-numeric-code-429") << sse({"{\"error\":{\"type\":\"invalid_request_error\","
                                                      "\"code\":429,\"message\":\"slow down\"}}"})
                                              << "slow down" << ProviderFailureKind::RateLimited;
        QTest::newRow("after-streamed-output")
            << sse({chatChunk(QStringLiteral("Half")),
                    json({{QStringLiteral("error"), QJsonObject{{QStringLiteral("message"),
                           QStringLiteral("chat_template_kwargs is not supported")}}}})})
            << "chat_template_kwargs is not supported" << ProviderFailureKind::Server;
        QTest::newRow("http-404") << httpResponse("404 Not Found", "application/json",
            "{\"error\":\"model \\\"m\\\" not found, try pulling it first\"}") << "try pulling it first"
            << ProviderFailureKind::Unavailable;
        QTest::newRow("token-limit") << sse({chatChunk(QStringLiteral("Half"), QStringLiteral("length"))})
                                     << "stopped early" << ProviderFailureKind::InvalidResult;
    }

    void reasoningFieldErrorsOnlyRetryHttp400()
    {
        QFETCH(QByteArray, response);
        QFETCH(QString, message);
        QFETCH(ProviderFailureKind, kind);
        FakeServer server;
        server.route("POST /v1/chat/completions", response);
        ChatCompletionsRefiner refiner(QStringLiteral("Custom endpoint"), ChatCompletionsRefiner::Audience::Server);
        QSignalSpy failed(&refiner, &ChatCompletionsRefiner::failed);
        QSignalSpy completed(&refiner, &ChatCompletionsRefiner::completed);
        const auto refine = [&] {
            refiner.refine(QStringLiteral("x"), {}, {}, {}, server.origin() + QStringLiteral("/v1"),
                           QStringLiteral("m"), QStringLiteral("balanced"), {});
        };
        refine();
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 2000);
        const ProviderFailure failure = failed.first().first().value<ProviderFailure>();
        QVERIFY2(failure.message.contains(message), qPrintable(failure.message));
        QCOMPARE(failure.kind, kind);
        QTest::qWait(100);
        QCOMPARE(server.requests.size(), 1);
        QCOMPARE(completed.size(), 0);
        refine();
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 2, 2000);
        QCOMPARE(server.requests.size(), 2);
        QVERIFY(server.jsonBody(1).contains(QStringLiteral("reasoning_effort")));
    }

    void anthropicFormatUsesAnApiKeyWithoutClaudeCodeHeaders()
    {
        FakeServer server;
        const QByteArray stream =
            "event: message_start\ndata: {\"type\":\"message_start\"}\n\n"
            "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"delta\":{\"type\":\"text_delta\",\"text\":\"Done.\"}}\n\n"
            "event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n";
        server.route("POST /v1/messages", httpResponse("200 OK", "text/event-stream", stream));
        server.route("POST /v1/messages",
                     httpResponse("200 OK", "text/event-stream",
                                  "event: message_delta\ndata: {\"delta\":{\"stop_reason\":\"max_tokens\"}}\n\n"));
        RefinementSettings settings;
        settings.endpoint.format = QStringLiteral("anthropic");
        settings.endpoint.baseUrl = server.origin() + QStringLiteral("/v1");
        settings.endpoint.model = QStringLiteral("qwen3.5:4b");
        settings.endpoint.apiKey = QStringLiteral("endpoint-key");
        EndpointTranscriptRefiner refiner;
        QSignalSpy completed(&refiner, &TranscriptRefiner::completed);
        QSignalSpy failed(&refiner, &TranscriptRefiner::failed);
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

        // Failures name the custom endpoint, not Anthropic.
        refiner.refine(QStringLiteral("x"), {}, context, settings);
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 2000);
        const QString message = failed.first().first().value<ProviderFailure>().message;
        QVERIFY2(message.startsWith(QStringLiteral("Custom Endpoint refinement")), qPrintable(message));
        QVERIFY(!message.contains(QStringLiteral("Anthropic")));
    }

    // A 404 means the endpoint no longer has the model or the path.
    void anthropicFormatEndpointWithoutTheModelIsUnavailable()
    {
        FakeServer server;
        server.route("POST /v1/messages", httpResponse("404 Not Found", "application/json",
            "{\"type\":\"error\",\"error\":{\"type\":\"not_found_error\",\"message\":\"model: m\"}}"));
        RefinementSettings settings;
        settings.endpoint.format = QStringLiteral("anthropic");
        settings.endpoint.baseUrl = server.origin() + QStringLiteral("/v1");
        settings.endpoint.model = QStringLiteral("m");
        EndpointTranscriptRefiner refiner;
        QSignalSpy failed(&refiner, &TranscriptRefiner::failed);
        refiner.refine(QStringLiteral("x"), {}, {}, settings);
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 2000);
        const ProviderFailure failure = failed.first().first().value<ProviderFailure>();
        QCOMPARE(failure.kind, ProviderFailureKind::Unavailable);
        QCOMPARE(failure.httpStatus, 404);
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

        // Still loading its model, llama-server answers 503 but is found;
        // whisper.cpp's server loading its model is not llama-server.
        FakeServer loading;
        loading.route("GET /health", httpResponse("503 Service Unavailable", "application/json",
                                                  "{\"error\":{\"message\":\"Loading model\","
                                                  "\"type\":\"unavailable_error\",\"code\":503}}"));
        QVERIFY(probeLlamaServer(loading.origin(), 2000));
        FakeServer whisper;
        whisper.route("GET /health", httpResponse("503 Service Unavailable", "application/json",
                                                  "{\"status\":\"loading model\"}"));
        QVERIFY(!probeLlamaServer(whisper.origin(), 2000));

        FakeServer lmStudio;
        lmStudio.route("GET /api/v1/models",
                       httpResponse("200 OK", "application/json",
                                    "{\"models\":[{\"type\":\"llm\",\"key\":\"google/gemma-4-e4b\"},"
                                    "{\"type\":\"embedding\",\"key\":\"nomic-embed\"}]}"));
        const std::optional<DetectedRunner> studio = probeLmStudio(lmStudio.origin(), 2000);
        QVERIFY(studio);
        QCOMPARE(studio->models, QStringList{QStringLiteral("google/gemma-4-e4b")});

        // LM Studio before 0.4 has only the v0 listing.
        FakeServer oldStudio;
        oldStudio.route("GET /api/v0/models",
                        httpResponse("200 OK", "application/json",
                                     "{\"data\":[{\"id\":\"qwen2-vl-7b-instruct\",\"type\":\"vlm\","
                                     "\"compatibility_type\":\"mlx\"},{\"id\":\"text-embedding-nomic\","
                                     "\"type\":\"embeddings\",\"compatibility_type\":\"gguf\"}]}"));
        const std::optional<DetectedRunner> old = probeLmStudio(oldStudio.origin(), 2000);
        QVERIFY(old);
        QCOMPARE(old->models, QStringList{QStringLiteral("qwen2-vl-7b-instruct")});
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
        // Set by the ctest that runs this suite against a real keyring.
        if (qEnvironmentVariableIntValue("SPEECHER_TEST_EXPECT_KEYRING") == 1) {
            QVERIFY(inKeyring);
        }

        // The startup prefetch fills the cache without blocking, so a
        // dictation finds keys it would otherwise wait for.
        SettingsStore prefetched;
        prefetched.secrets()->prefetch();
        QTRY_COMPARE_WITH_TIMEOUT(prefetched.dictationSnapshot().speech.endpoint.apiKey,
                                  QStringLiteral("speech-secret"), 3000);

        draft = loaded;
        draft.speech.endpoint.apiKey.clear();
        draft.refinement.endpoint.apiKey.clear();
        reopened.applySnapshot(draft);
        QCOMPARE(SettingsStore().snapshot().speech.endpoint.apiKey, QString());
        QVERIFY(!reopened.raw().contains(SettingsKeys::SpeechEndpointApiKey));
        QVERIFY(!reopened.raw().contains(SettingsKeys::SecretsInKeyring));
    }

    void unreadDraftSurvivesPrefetchCompletion()
    {
        if (qEnvironmentVariableIntValue("SPEECHER_TEST_EXPECT_KEYRING") != 1)
            QSKIP("Requires the private keyring suite");
        SettingsStore saved;
        saved.raw().clear();
        saved.setCliproxyApiKey(QStringLiteral("keep-me"));
        SettingsStore settings;
        qputenv("SPEECHER_TEST_KEYRING_READ_TIMEOUT", "1");
        const AppSettings draft = settings.snapshot();
        qunsetenv("SPEECHER_TEST_KEYRING_READ_TIMEOUT");
        QVERIFY(draft.refinement.cliproxyApiKey.isEmpty());
        settings.secrets()->prefetch();
        QTRY_COMPARE(settings.secrets()->cachedSecret(SecretStore::Secret::CliproxyApiKey),
                     QStringLiteral("keep-me"));
        settings.applySnapshot(draft);
        QCOMPARE(SettingsStore().cliproxyApiKey(), QStringLiteral("keep-me"));
        saved.setCliproxyApiKey({});
    }

    void failedDeletionStaysClearedAfterRestart()
    {
        if (qEnvironmentVariableIntValue("SPEECHER_TEST_EXPECT_KEYRING") != 1)
            QSKIP("Requires the private keyring suite");
        SettingsStore settings;
        settings.raw().clear();
        settings.setCliproxyApiKey(QStringLiteral("remove-me"));
        qputenv("SPEECHER_TEST_KEYRING_DELETE_FAILURE", "1");
        const auto restore = qScopeGuard([] { qunsetenv("SPEECHER_TEST_KEYRING_DELETE_FAILURE"); });
        QVERIFY(settings.secrets()->saveSecret(SecretStore::Secret::CliproxyApiKey, {}));
        QVERIFY(settings.secrets()->lastError().contains(QStringLiteral("denied")));
        SettingsStore reopened;
        reopened.secrets()->prefetch();
        QCOMPARE(reopened.cliproxyApiKey(), QString());
        QCOMPARE(reopened.snapshot().refinement.cliproxyApiKey, QString());
    }

    // Each provider's key is read on the worker when that provider is tried,
    // and one that can't be read fails only that provider: a refiner's never
    // stops dictation from starting.
    void dictationResolvesOnlySelectedSecretsOffThread_data()
    {
        QTest::addColumn<QString>("refiner");
        QTest::addColumn<QString>("speechProvider");
        QTest::addColumn<bool>("remote");
        QTest::addColumn<QString>("unavailable");
        const QString speech = QStringLiteral("speech");
        const QString refinement = QStringLiteral("refinement");
        QTest::newRow("openai-proxy") << QStringLiteral("openai") << QStringLiteral("claude") << true << refinement;
        QTest::newRow("anthropic-proxy") << QStringLiteral("anthropic") << QStringLiteral("claude") << true << refinement;
        QTest::newRow("proxy-preset") << QStringLiteral("endpoint") << QStringLiteral("claude") << true << refinement;
        QTest::newRow("inherited-proxy-key") << QStringLiteral("endpoint-inherited") << QStringLiteral("claude") << true << refinement;
        QTest::newRow("speech-endpoint") << QStringLiteral("none") << QStringLiteral("endpoint") << true << speech;
        QTest::newRow("none-inactive-proxy") << QStringLiteral("none") << QStringLiteral("claude") << true << QString();
        QTest::newRow("local-inactive-proxy") << QStringLiteral("local") << QStringLiteral("claude") << true << QString();
        QTest::newRow("local-account-files") << QStringLiteral("openai") << QStringLiteral("claude") << false << QString();
    }

    void dictationResolvesOnlySelectedSecretsOffThread()
    {
        QFETCH(QString, refiner);
        QFETCH(QString, speechProvider);
        QFETCH(bool, remote);
        QFETCH(QString, unavailable);
        {
            SettingsStore reset;
            reset.raw().clear();
        }
        SettingsStore settings;
        settings.setRefinementProvider(refiner == QStringLiteral("endpoint-inherited") ? QStringLiteral("endpoint") : refiner);
        settings.setSpeechProvider(speechProvider);
        settings.setOpenAiAuthMode(QStringLiteral("cliproxy"));
        settings.setAnthropicAuthMode(QStringLiteral("cliproxy"));
        settings.setCliproxyBaseUrl(remote ? QStringLiteral("http://proxy.example:8317") : QString());
        settings.raw().setValue(SettingsKeys::RefinementEndpointPreset,
                                refiner == QStringLiteral("endpoint-inherited") ? QString() : QStringLiteral("cliproxy"));
        settings.raw().setValue(SettingsKeys::RefinementEndpointUseCliproxyKey, refiner == QStringLiteral("endpoint-inherited"));
        settings.raw().setValue(SettingsKeys::SpeechEndpointBaseUrl, QStringLiteral("http://speech.example"));
        settings.raw().setValue(SettingsKeys::SecretsInKeyring,
            QStringList{QStringLiteral("cliproxy-api-key"), QStringLiteral("speech-endpoint-key")});
        settings.raw().sync();
        qputenv("SPEECHER_TEST_KEYRING_READ_TIMEOUT", "1");
        qputenv("SPEECHER_TEST_KEYRING_READ_DELAY_MS", "250");
        // A read on the GUI thread fails with its own reason instead of timing out.
        qputenv("SPEECHER_TEST_KEYRING_REFUSE_GUI_THREAD", "1");
        const auto restore = qScopeGuard([] {
            qunsetenv("SPEECHER_TEST_KEYRING_READ_TIMEOUT");
            qunsetenv("SPEECHER_TEST_KEYRING_READ_DELAY_MS");
            qunsetenv("SPEECHER_TEST_KEYRING_REFUSE_GUI_THREAD");
        });
        FakeAudioInput audio;
        FakeMediaController media;
        FakeDelivery delivery;
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        registry.registerSpeechProvider({QStringLiteral("endpoint"), QStringLiteral("Endpoint")},
            [](QObject *parent) { return new EndpointSpeechTranscriber(parent); });
        FakeRefiner *fakeRefiner = nullptr;
        registerFakeRefiner(registry, &fakeRefiner,
                            refiner == QStringLiteral("endpoint-inherited") ? QStringLiteral("endpoint") : refiner);
        DictationSession session(&settings, &audio, &media, &delivery, &registry);
        session.startListening();
        QVERIFY(settings.secrets()->lastError().isEmpty());
        if (unavailable == QStringLiteral("refinement")) {
            QTRY_COMPARE_WITH_TIMEOUT(session.state(), DictationState::Listening, 200);
            speech->emitFinalText(QStringLiteral("spoken words"));
            session.stopListening();
            // The read runs on the worker while the session waits in Refining.
            QCOMPARE(session.state(), DictationState::Refining);
            QTRY_COMPARE_WITH_TIMEOUT(delivery.calls, 1, 2000);
            QCOMPARE(delivery.lastText, QStringLiteral("spoken words"));
            QVERIFY2(session.lastMessage().startsWith(QStringLiteral("Used raw transcript")),
                     qPrintable(session.lastMessage()));
            QCOMPARE(fakeRefiner->prepareCalls, 0);
            QVERIFY(settings.secrets()->lastError().isEmpty());
        } else if (unavailable == QStringLiteral("speech")) {
            // The GUI thread goes on handling events while the worker reads:
            // a call queued now runs before the read's answer arrives.
            QCOMPARE(session.state(), DictationState::Starting);
            std::optional<DictationState> stateWhileReading;
            QMetaObject::invokeMethod(this, [&] { stateWhileReading = session.state(); },
                                      Qt::QueuedConnection);
            QTRY_COMPARE_WITH_TIMEOUT(session.state(), DictationState::Error, 2000);
            QCOMPARE(stateWhileReading, std::optional(DictationState::Starting));
            QVERIFY2(session.lastMessage().contains(QStringLiteral("keyring unavailable")),
                     qPrintable(session.lastMessage()));
            QVERIFY2(session.lastMessage().contains(QStringLiteral("timed out")),
                     qPrintable(session.lastMessage()));
            QVERIFY(!session.lastMessage().contains(QStringLiteral("not set")));
            QVERIFY(!audio.started);
        } else {
            QTRY_COMPARE_WITH_TIMEOUT(session.state(), DictationState::Listening, 200);
        }
        session.cancelForShutdown();
    }

    void dictationUsesTheKeyResolvedDuringStartup()
    {
        if (qEnvironmentVariableIntValue("SPEECHER_TEST_EXPECT_KEYRING") != 1)
            QSKIP("Requires the private keyring suite");
        FakeServer server;
        server.route("POST /v1/audio/transcriptions",
                     httpResponse("200 OK", "application/json", "{\"text\":\"Hello\"}"));
        {
            SettingsStore saved;
            saved.raw().clear();
            saved.setSpeechProvider(QStringLiteral("endpoint"));
            saved.setRefinementProvider(QStringLiteral("none"));
            saved.raw().setValue(SettingsKeys::SpeechEndpointBaseUrl, server.origin());
            QVERIFY(saved.secrets()->saveSecret(SecretStore::Secret::SpeechEndpointKey, QStringLiteral("speech-key")));
            saved.raw().sync();
        }
        SettingsStore settings;
        qputenv("SPEECHER_TEST_KEYRING_READ_DELAY_MS", "250");
        const auto restore = qScopeGuard([] { qunsetenv("SPEECHER_TEST_KEYRING_READ_DELAY_MS"); });
        FakeAudioInput audio;
        FakeMediaController media;
        FakeDelivery delivery;
        ProviderRegistry registry;
        registry.registerSpeechProvider({QStringLiteral("endpoint"), QStringLiteral("Endpoint")},
            [](QObject *parent) { return new EndpointSpeechTranscriber(parent); });
        DictationSession session(&settings, &audio, &media, &delivery, &registry);
        QElapsedTimer elapsed;
        elapsed.start();
        session.startListening();
        QVERIFY(elapsed.elapsed() < 100);
        QTRY_COMPARE_WITH_TIMEOUT(session.state(), DictationState::Listening, 2000);
        audio.pushAudio(QByteArray(640, '\0'));
        session.stopListening();
        QTRY_COMPARE_WITH_TIMEOUT(delivery.calls, 1, 2000);
        QCOMPARE(delivery.lastText, QStringLiteral("Hello"));
        QCOMPARE(server.requests.size(), 1);
        QVERIFY(headersOf(server.requests.first()).contains("authorization: bearer speech-key"));
        session.cancelForShutdown();
        settings.secrets()->secret(SecretStore::Secret::SpeechEndpointKey);
        settings.secrets()->saveSecret(SecretStore::Secret::SpeechEndpointKey, {});
    }

    // The settings window's snapshot shows a saved key for an endpoint that
    // is not selected; dictation start does not wait for that key; and when
    // the keyring does not answer, switching to the endpoint and saving the
    // empty field keeps the stored key.
    void onlyTheSettingsSnapshotWaitsForUnselectedKeys()
    {
        {
            SettingsStore settings;
            settings.raw().clear();
            // As SecretStore records a key it wrote to the keyring.
            settings.raw().setValue(SettingsKeys::SecretsInKeyring,
                                    QStringList{QStringLiteral("speech-endpoint-key")});
            settings.raw().sync();
        }
        qputenv("SPEECHER_TEST_KEYRING_READ_TIMEOUT", "1");
        const auto restore = qScopeGuard([] { qunsetenv("SPEECHER_TEST_KEYRING_READ_TIMEOUT"); });

        SettingsStore settings;
        QVERIFY(settings.speechProvider() != QStringLiteral("endpoint"));
        settings.dictationSnapshot();
        QVERIFY2(settings.secrets()->lastError().isEmpty(), qPrintable(settings.secrets()->lastError()));

        AppSettings draft = settings.snapshot();
        QVERIFY(settings.secrets()->lastError().contains(QStringLiteral("timed out")));
        QCOMPARE(draft.speech.endpoint.apiKey, QString());

        draft.speech.providerId = QStringLiteral("endpoint");
        draft.speech.endpoint.baseUrl = QStringLiteral("http://whisper.local:8080");
        settings.applySnapshot(draft);
        QCOMPARE(settings.speechProvider(), QStringLiteral("endpoint"));
        QCOMPARE(settings.raw().value(SettingsKeys::SecretsInKeyring).toStringList(),
                 QStringList{QStringLiteral("speech-endpoint-key")});
        QVERIFY(!settings.raw().contains(SettingsKeys::SpeechEndpointApiKey));
    }

    void constructingSettingsDoesNotTouchTheKeyring()
    {
        const QString pendingDeletion = SettingsKeys::RefinementEndpointApiKey + QStringLiteral("PendingDeletion");
        {
            SettingsStore settings;
            settings.raw().clear();
            settings.raw().setValue(SettingsKeys::CliproxyApiKey, QStringLiteral("legacy-proxy-key"));
            settings.raw().setValue(pendingDeletion, true);
            settings.raw().sync();
        }
        qputenv("SPEECHER_TEST_KEYRING_DELETE_FAILURE", "1");
        const auto restore = qScopeGuard([] { qunsetenv("SPEECHER_TEST_KEYRING_DELETE_FAILURE"); });

        SettingsStore settings;

        QVERIFY2(settings.secrets()->lastError().isEmpty(), "SettingsStore construction touched the keyring");
        QCOMPARE(settings.raw().value(SettingsKeys::CliproxyApiKey).toString(), QStringLiteral("legacy-proxy-key"));
        QVERIFY(settings.raw().value(pendingDeletion).toBool());
        QVERIFY(!settings.raw().contains(SettingsKeys::SecretsInKeyring));
        settings.raw().clear();
    }

    // Existing CLI Proxy API users keep their key: it moves from the settings
    // file to the keyring on controller startup when a keyring accepts it.
    void cliproxyKeyMigratesOutOfTheSettingsFile()
    {
        {
            SettingsStore settings;
            settings.raw().clear();
            settings.raw().setValue(SettingsKeys::CliproxyBaseUrl, QStringLiteral("http://proxy.example:8317"));
            settings.raw().setValue(SettingsKeys::CliproxyApiKey, QStringLiteral(" legacy-proxy-key "));
            settings.raw().sync();
        }
        ApplicationController controller(true);
        SettingsStore &settings = *controller.settings();
        QCOMPARE(settings.cliproxyApiKey(), QStringLiteral("legacy-proxy-key"));
        QCOMPARE(settings.snapshot().refinement.cliproxyApiKey, QStringLiteral("legacy-proxy-key"));
        const bool migrated = settings.raw().value(SettingsKeys::SecretsInKeyring).toStringList()
                                  .contains(QStringLiteral("cliproxy-api-key"));
        QCOMPARE(settings.raw().contains(SettingsKeys::CliproxyApiKey), !migrated);
        if (qEnvironmentVariableIntValue("SPEECHER_TEST_EXPECT_KEYRING") == 1) {
            QVERIFY(migrated);
        }
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
