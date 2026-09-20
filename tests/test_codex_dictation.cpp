#include "common/test_prelude.h"
#include "common/test_auth.h"
#include "common/test_http.h"

#include <QScopeGuard>
#include <QTcpServer>
#include "providers/ClaudeSpeechTranscriber.h"
#include "providers/CodexDictationClient.h"
#include "providers/CodexSpeechTranscriber.h"

using namespace speecher::test;

class CodexDictationTests : public QObject {
    Q_OBJECT

private slots:
#ifdef SPEECHER_WITH_QT_WEBSOCKETS
    void clientMatchesTheCodexDictationProtocol()
    {
        QWebSocketServer server(QStringLiteral("speecher-test"), QWebSocketServer::NonSecureMode);
        server.setSupportedSubprotocols({QStringLiteral("openai-bearer.test-token")});
        QVERIFY(server.listen(QHostAddress::LocalHost));

        QWebSocket *peer = nullptr;
        QStringList messages;
        connect(&server, &QWebSocketServer::newConnection, this, [&] {
            peer = server.nextPendingConnection();
            connect(peer, &QWebSocket::textMessageReceived, this,
                    [&messages](const QString &message) { messages.append(message); });
        });

        CodexDictationClient client;
        QSignalSpy connected(&client, &CodexDictationClient::connected);
        QSignalSpy final(&client, &CodexDictationClient::finalTranscript);
        QSignalSpy completed(&client, &CodexDictationClient::completed);
        QSignalSpy failed(&client, &CodexDictationClient::failed);
        client.start(QUrl(QStringLiteral("ws://127.0.0.1:%1/dictation/stream")
                              .arg(server.serverPort())),
                     QStringLiteral("test-token"),
                     16000);

        QTRY_VERIFY_WITH_TIMEOUT(peer, 1000);
        QCOMPARE(peer->subprotocol(), QStringLiteral("openai-bearer.test-token"));
        QVERIFY(peer->request().header(QNetworkRequest::UserAgentHeader)
                    .toString().contains(QStringLiteral("Chrome/")));
        QTRY_COMPARE_WITH_TIMEOUT(messages.size(), 1, 1000);
        const QJsonObject start = QJsonDocument::fromJson(messages.takeFirst().toUtf8()).object();
        QCOMPARE(start.value(QStringLiteral("type")).toString(), QStringLiteral("session.start"));
        const QJsonObject config = start.value(QStringLiteral("config")).toObject();
        QCOMPARE(config.value(QStringLiteral("input_audio_format")).toString(), QStringLiteral("pcm16"));
        QCOMPARE(config.value(QStringLiteral("sample_rate_hz")).toInt(), 16000);
        QCOMPARE(config.value(QStringLiteral("num_channels")).toInt(), 1);
        QCOMPARE(config.value(QStringLiteral("max_buffer_size_bytes")).toInt(), 4 * 1024 * 1024);
        QCOMPARE(config.value(QStringLiteral("max_utterance_duration_ms")).toInt(), 30000);
        QCOMPARE(config.value(QStringLiteral("session_ttl_ms")).toInt(), 300000);
        QCOMPARE(config.value(QStringLiteral("provider_mode")).toString(), QStringLiteral("streaming_sse"));
        QCOMPARE(config.value(QStringLiteral("transcript_delivery_mode")).toString(), QStringLiteral("segment"));
        const QJsonObject vad = config.value(QStringLiteral("vad")).toObject();
        QCOMPARE(vad.value(QStringLiteral("type")).toString(), QStringLiteral("server_vad"));
        QCOMPARE(vad.value(QStringLiteral("threshold")).toDouble(), 0.5);
        QCOMPARE(vad.value(QStringLiteral("prefix_padding_ms")).toInt(), 300);
        QCOMPARE(vad.value(QStringLiteral("silence_duration_ms")).toInt(), 500);

        peer->sendTextMessage(QStringLiteral(
            R"({"type":"session.started","sequence_no":1,"session":{"session_id":"s1","status":"active","config":{"provider_mode":"streaming_sse","transcript_delivery_mode":"final_only"}}})"));
        QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 1000);

        const QByteArray pcm = QByteArray::fromHex("0102ff00");
        client.sendAudio(pcm);
        QTRY_COMPARE_WITH_TIMEOUT(messages.size(), 1, 1000);
        const QJsonObject append = QJsonDocument::fromJson(messages.takeFirst().toUtf8()).object();
        QCOMPARE(append.value(QStringLiteral("type")).toString(), QStringLiteral("audio.append"));
        QCOMPARE(QByteArray::fromBase64(append.value(QStringLiteral("audio")).toString().toUtf8()), pcm);

        QSignalSpy partial(&client, &CodexDictationClient::partialTranscript);
        peer->sendTextMessage(QStringLiteral(
            R"({"type":"transcript.segment","sequence_no":2,"utterance_id":"u1","revision":1,"text":" hello"})"));
        QTRY_COMPARE_WITH_TIMEOUT(partial.count(), 1, 1000);
        QCOMPARE(partial.first().first().toString(), QStringLiteral("hello"));
        peer->sendTextMessage(QStringLiteral(
            R"({"type":"transcript.segment","sequence_no":2,"utterance_id":"u1","revision":2,"text":" hello from"})"));
        QTRY_COMPARE_WITH_TIMEOUT(partial.count(), 2, 1000);
        QCOMPARE(partial.last().first().toString(), QStringLiteral("hello from"));

        peer->sendTextMessage(QStringLiteral(
            R"({"type":"transcript.final","sequence_no":2,"utterance_id":"u1","revision":1,"text":"hello from Codex"})"));
        QTRY_COMPARE_WITH_TIMEOUT(final.count(), 1, 1000);
        QCOMPARE(final.first().first().toString(), QStringLiteral("hello from Codex"));
        peer->sendTextMessage(QStringLiteral(
            R"({"type":"transcript.final","sequence_no":2,"utterance_id":"u1","revision":1,"text":"hello from Codex"})"));
        QTest::qWait(20);
        QCOMPARE(final.count(), 1);
        peer->sendTextMessage(QStringLiteral(
            R"({"type":"transcript.segment","sequence_no":3,"utterance_id":"u1","revision":3,"text":" stale segment"})"));
        QTest::qWait(20);
        QCOMPARE(partial.count(), 2);

        client.stop();
        QTRY_COMPARE_WITH_TIMEOUT(messages.size(), 2, 1000);
        QCOMPARE(QJsonDocument::fromJson(messages.at(0).toUtf8()).object(),
                 QJsonObject({{QStringLiteral("type"), QStringLiteral("audio.flush")},
                              {QStringLiteral("reason"), QStringLiteral("client")}}));
        QCOMPARE(QJsonDocument::fromJson(messages.at(1).toUtf8()).object(),
                 QJsonObject({{QStringLiteral("type"), QStringLiteral("session.close")}}));
        peer->sendTextMessage(QStringLiteral(
            R"({"type":"session.updated","sequence_no":3,"session":{"session_id":"s1","status":"closed","config":{"provider_mode":"streaming_sse","transcript_delivery_mode":"final_only"}}})"));
        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 1000);
        QCOMPARE(failed.count(), 0);
        peer->deleteLater();
    }

    void codexDictationClientReportsUnexpectedRemoteClose()
    {
        QWebSocketServer server(QStringLiteral("speecher-test"), QWebSocketServer::NonSecureMode);
        server.setSupportedSubprotocols({QStringLiteral("openai-bearer.test-token")});
        QVERIFY(server.listen(QHostAddress::LocalHost));

        CodexDictationClient client;
        QSignalSpy connected(&client, &CodexDictationClient::connected);
        QSignalSpy failed(&client, &CodexDictationClient::failed);
        client.start(QUrl(QStringLiteral("ws://127.0.0.1:%1/dictation/stream")
                              .arg(server.serverPort())),
                     QStringLiteral("test-token"),
                     16000);
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
        std::unique_ptr<QWebSocket> peer(server.nextPendingConnection());
        peer->sendTextMessage(QStringLiteral(
            R"({"type":"session.started","sequence_no":1,"session":{"session_id":"s1","status":"active","config":{"provider_mode":"streaming_sse","transcript_delivery_mode":"final_only"}}})"));
        QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 1000);

        peer->close();

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 1000);
        QCOMPARE(failed.first().at(1).toBool(), true);
        QCOMPARE(failed.first().at(2).toString(), QStringLiteral("streaming"));
    }

    void speechTranscribersLoadCliproxyAccounts()
    {
        QTemporaryDir dir;
        const QDateTime valid = QDateTime::currentDateTimeUtc().addSecs(3600);
        QVERIFY(writeCliProxyAccount(dir.path(), QStringLiteral("codex-a@example.com.json"),
                                     QStringLiteral("codex"), QStringLiteral("codex-token"), valid));
        QVERIFY(writeCliProxyAccount(dir.path(), QStringLiteral("claude-a@example.com.json"),
                                     QStringLiteral("claude"), QStringLiteral("claude-token"), valid));

        SpeechSettings settings;
        settings.claudeAuthMode = QStringLiteral("cliproxy");
        settings.codexAuthMode = QStringLiteral("cliproxy");
        settings.cliproxyOauthDir = dir.path();

        CodexSpeechTranscriber codex;
        QVERIFY(codex.prepare(settings).ok);
        QVERIFY(!codex.requiresRefresh(settings));

        ClaudeSpeechTranscriber claude;
        QVERIFY(claude.prepare(settings).ok);
        QVERIFY(!claude.requiresRefresh(settings));

        settings.cliproxyOauthDir = QStringLiteral("/nonexistent/oauth");
        const SpeechPrepareResult missing = codex.prepare(settings);
        QVERIFY(!missing.ok);
        QVERIFY(missing.message.contains(QStringLiteral("No CLI Proxy API codex accounts")));
    }

    void finalRetranscribeReplacesTranscriptFromBatchEndpoint()
    {
        QWebSocketServer server(QStringLiteral("speecher-test"), QWebSocketServer::NonSecureMode);
        server.setSupportedSubprotocols({QStringLiteral("chatgpt-dictation")});
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QTcpServer http;
        QVERIFY(http.listen(QHostAddress::LocalHost));
        qputenv("SPEECHER_CODEX_DICTATION_URL",
                QStringLiteral("ws://127.0.0.1:%1/dictation/stream")
                    .arg(server.serverPort()).toUtf8());
        qputenv("SPEECHER_CODEX_TRANSCRIBE_URL",
                QStringLiteral("http://127.0.0.1:%1/transcribe")
                    .arg(http.serverPort()).toUtf8());
        const auto unsetEnv = qScopeGuard([] {
            qunsetenv("SPEECHER_CODEX_DICTATION_URL");
            qunsetenv("SPEECHER_CODEX_TRANSCRIBE_URL");
        });

        QWebSocket *peer = nullptr;
        QStringList messages;
        connect(&server, &QWebSocketServer::newConnection, this, [&] {
            peer = server.nextPendingConnection();
            connect(peer, &QWebSocket::textMessageReceived, this,
                    [&messages](const QString &message) { messages.append(message); });
        });

        CodexSpeechTranscriber transcriber;
        QSignalSpy finals(&transcriber, &SpeechTranscriber::finalTranscript);
        QSignalSpy attemptText(&transcriber, &SpeechTranscriber::attemptTranscript);
        QSignalSpy completed(&transcriber, &SpeechTranscriber::attemptCompleted);
        SpeechSettings settings;
        settings.codexFinalRetranscribe = true;
        transcriber.startAttempt(9, settings);

        QTRY_VERIFY_WITH_TIMEOUT(peer, 1000);
        peer->sendTextMessage(QStringLiteral(
            R"({"type":"session.started","sequence_no":1,"session":{"session_id":"s1","status":"active","config":{}}})"));

        const QByteArray pcm = QByteArray::fromHex("0102ff007fff8000");
        transcriber.sendAudio(9, pcm);
        QTRY_COMPARE_WITH_TIMEOUT(messages.size(), 2, 1000); // session.start + audio.append
        transcriber.finishInput(9);
        peer->sendTextMessage(QStringLiteral(
            R"({"type":"transcript.final","sequence_no":2,"utterance_id":"u1","revision":1,"text":"streamed text"})"));
        peer->sendTextMessage(QStringLiteral(
            R"({"type":"session.updated","sequence_no":3,"session":{"session_id":"s1","status":"closed","config":{}}})"));
        QTRY_COMPARE_WITH_TIMEOUT(finals.count(), 1, 1000);

        QTRY_VERIFY_WITH_TIMEOUT(http.hasPendingConnections(), 2000);
        std::unique_ptr<QTcpSocket> request(http.nextPendingConnection());
        const QByteArray received = readHttpRequest(request.get(), 2000);
        QVERIFY(received.startsWith("POST /transcribe"));
        QVERIFY(received.contains("Authorization: Bearer"));
        QVERIFY(received.contains("filename=\"dictation.wav\""));
        QVERIFY(received.contains("RIFF"));
        QVERIFY(received.contains(pcm));
        const QByteArray body = QByteArrayLiteral(R"({"text":" batch text "})");
        request->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                       + QByteArray::number(body.size()) + "\r\n\r\n" + body);
        request->flush();

        QTRY_COMPARE_WITH_TIMEOUT(attemptText.count(), 1, 2000);
        QCOMPARE(attemptText.first().at(1).toString(), QStringLiteral("batch text"));
        QCOMPARE(completed.count(), 1);
        peer->deleteLater();
    }

    void cancelDuringFinalRetranscribeEmitsNothing()
    {
        QWebSocketServer server(QStringLiteral("speecher-test"), QWebSocketServer::NonSecureMode);
        server.setSupportedSubprotocols({QStringLiteral("chatgpt-dictation")});
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QTcpServer http;
        QVERIFY(http.listen(QHostAddress::LocalHost));
        qputenv("SPEECHER_CODEX_DICTATION_URL",
                QStringLiteral("ws://127.0.0.1:%1/dictation/stream")
                    .arg(server.serverPort()).toUtf8());
        qputenv("SPEECHER_CODEX_TRANSCRIBE_URL",
                QStringLiteral("http://127.0.0.1:%1/transcribe")
                    .arg(http.serverPort()).toUtf8());
        const auto unsetEnv = qScopeGuard([] {
            qunsetenv("SPEECHER_CODEX_DICTATION_URL");
            qunsetenv("SPEECHER_CODEX_TRANSCRIBE_URL");
        });

        CodexSpeechTranscriber transcriber;
        QSignalSpy attemptText(&transcriber, &SpeechTranscriber::attemptTranscript);
        QSignalSpy completed(&transcriber, &SpeechTranscriber::attemptCompleted);
        SpeechSettings settings;
        settings.codexFinalRetranscribe = true;
        transcriber.startAttempt(9, settings);

        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
        std::unique_ptr<QWebSocket> peer(server.nextPendingConnection());
        peer->sendTextMessage(QStringLiteral(
            R"({"type":"session.started","sequence_no":1,"session":{"session_id":"s1","status":"active","config":{}}})"));
        transcriber.sendAudio(9, QByteArray::fromHex("0102ff00"));
        transcriber.finishInput(9);
        peer->sendTextMessage(QStringLiteral(
            R"({"type":"session.updated","sequence_no":2,"session":{"session_id":"s1","status":"closed","config":{}}})"));

        QTRY_VERIFY_WITH_TIMEOUT(http.hasPendingConnections(), 2000);
        std::unique_ptr<QTcpSocket> request(http.nextPendingConnection());
        transcriber.cancelAttempt(9);
        const QByteArray body = QByteArrayLiteral(R"({"text":"too late"})");
        request->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                       + QByteArray::number(body.size()) + "\r\n\r\n" + body);
        request->flush();

        QTest::qWait(200);
        QCOMPARE(attemptText.count(), 0);
        QCOMPARE(completed.count(), 0);
        peer->deleteLater();
    }

    void finalRetranscribeFailureKeepsStreamedTranscript()
    {
        QWebSocketServer server(QStringLiteral("speecher-test"), QWebSocketServer::NonSecureMode);
        server.setSupportedSubprotocols({QStringLiteral("chatgpt-dictation")});
        QVERIFY(server.listen(QHostAddress::LocalHost));
        // A closed port: the accuracy pass must fail without failing dictation.
        QTcpServer unusedPort;
        QVERIFY(unusedPort.listen(QHostAddress::LocalHost));
        const quint16 closedPort = unusedPort.serverPort();
        unusedPort.close();
        qputenv("SPEECHER_CODEX_DICTATION_URL",
                QStringLiteral("ws://127.0.0.1:%1/dictation/stream")
                    .arg(server.serverPort()).toUtf8());
        qputenv("SPEECHER_CODEX_TRANSCRIBE_URL",
                QStringLiteral("http://127.0.0.1:%1/transcribe").arg(closedPort).toUtf8());
        const auto unsetEnv = qScopeGuard([] {
            qunsetenv("SPEECHER_CODEX_DICTATION_URL");
            qunsetenv("SPEECHER_CODEX_TRANSCRIBE_URL");
        });

        CodexSpeechTranscriber transcriber;
        QSignalSpy attemptText(&transcriber, &SpeechTranscriber::attemptTranscript);
        QSignalSpy completed(&transcriber, &SpeechTranscriber::attemptCompleted);
        QSignalSpy failed(&transcriber, &SpeechTranscriber::failed);
        SpeechSettings settings;
        settings.codexFinalRetranscribe = true;
        transcriber.startAttempt(9, settings);

        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
        std::unique_ptr<QWebSocket> peer(server.nextPendingConnection());
        peer->sendTextMessage(QStringLiteral(
            R"({"type":"session.started","sequence_no":1,"session":{"session_id":"s1","status":"active","config":{}}})"));
        transcriber.sendAudio(9, QByteArray::fromHex("0102ff00"));
        transcriber.finishInput(9);
        peer->sendTextMessage(QStringLiteral(
            R"({"type":"session.updated","sequence_no":2,"session":{"session_id":"s1","status":"closed","config":{}}})"));

        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 3000);
        QCOMPARE(attemptText.count(), 0);
        QCOMPARE(failed.count(), 0);
    }

    void liveCodexDictationProvider()
    {
        const QString pcmPath = qEnvironmentVariable("SPEECHER_TEST_LIVE_CODEX_PCM");
        if (pcmPath.isEmpty()) {
            QSKIP("Live Codex dictation check is opt-in");
        }

        QFile pcmFile(pcmPath);
        QVERIFY2(pcmFile.open(QIODevice::ReadOnly), "Could not read live Codex PCM input");
        const QByteArray pcm = pcmFile.readAll();
        QVERIFY2(pcm.size() >= 3200 && pcm.size() % 2 == 0,
                 "Live Codex PCM must be mono 16 kHz signed 16-bit raw audio");

        CodexSpeechTranscriber transcriber;
        SpeechSettings settings = SettingsStore().snapshot().speech;
        const SpeechPrepareResult prepared = transcriber.prepare(settings);
        QVERIFY2(prepared.ok, qPrintable(prepared.message));

        QSignalSpy partial(&transcriber, &SpeechTranscriber::partialTranscript);
        QSignalSpy final(&transcriber, &SpeechTranscriber::finalTranscript);
        QSignalSpy attemptText(&transcriber, &SpeechTranscriber::attemptTranscript);
        QSignalSpy completed(&transcriber, &SpeechTranscriber::attemptCompleted);
        QSignalSpy failed(&transcriber, &SpeechTranscriber::failed);
        transcriber.startAttempt(7, settings);

        QTimer sender;
        sender.setInterval(100);
        qsizetype offset = 0;
        connect(&sender, &QTimer::timeout, &transcriber,
                [&transcriber, &sender, &pcm, &offset] {
                    constexpr qsizetype bytesPerTick = 3200;
                    const QByteArray chunk = pcm.mid(offset, bytesPerTick);
                    offset += chunk.size();
                    if (!chunk.isEmpty()) {
                        transcriber.sendAudio(7, chunk);
                    }
                    if (offset >= pcm.size()) {
                        sender.stop();
                        transcriber.finishInput(7);
                    }
                });
        sender.start();

        const int audioDurationMs = qRound(pcm.size() * 1000.0 / 32000.0);
        QTRY_VERIFY_WITH_TIMEOUT(!completed.isEmpty() || !failed.isEmpty(),
                                 audioDurationMs + 15000);
        const QString failure = failed.isEmpty()
            ? QString()
            : failed.first().first().value<SpeechFailure>().message;
        QVERIFY2(failed.isEmpty(), qPrintable(failure));
        QCOMPARE(completed.count(), 1);
        QVERIFY(!final.isEmpty());
        QVERIFY(!final.last().at(1).toString().trimmed().isEmpty());
        QVERIFY2(partial.count() > 0,
                 "No live partial transcripts arrived while streaming (preview broken)");
        if (settings.codexFinalRetranscribe) {
            QVERIFY2(attemptText.count() == 1,
                     "The accuracy pass did not deliver a whole-attempt transcript");
            const QString batch = attemptText.first().at(1).toString();
            qInfo("live streamed final: %s", qPrintable(final.last().at(1).toString()));
            qInfo("live accuracy pass: %s", qPrintable(batch));
            QVERIFY(!batch.trimmed().isEmpty());
        }
    }
#endif
};

int runCodexDictationTests(int argc, char **argv)
{
    CodexDictationTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_codex_dictation.moc"
