#include "common/test_doubles.h"
#include "common/test_http.h"
#include "common/test_auth.h"

using namespace speecher::test;


class ClaudeVoiceTests : public QObject {
    Q_OBJECT

private slots:
    void claudeVoiceStreamQueryMatchesClaudeCode()
    {
        const QUrlQuery query = claudeVoiceStreamQuery(QStringList{
            QStringLiteral("Deepgram Nova 3"),
            QStringLiteral("Speecher"),
        });

        QCOMPARE(query.queryItemValue(QStringLiteral("encoding")), QStringLiteral("linear16"));
        QCOMPARE(query.queryItemValue(QStringLiteral("sample_rate")), QStringLiteral("16000"));
        QCOMPARE(query.queryItemValue(QStringLiteral("channels")), QStringLiteral("1"));
        QCOMPARE(query.queryItemValue(QStringLiteral("endpointing_ms")), QStringLiteral("300"));
        QCOMPARE(query.queryItemValue(QStringLiteral("utterance_end_ms")), QStringLiteral("1000"));
        QCOMPARE(query.queryItemValue(QStringLiteral("language")), QStringLiteral("en"));
        QCOMPARE(query.queryItemValue(QStringLiteral("use_conversation_engine")), QStringLiteral("true"));
        QCOMPARE(query.queryItemValue(QStringLiteral("forward_interims")), QStringLiteral("typed"));
        QCOMPARE(query.queryItemValue(QStringLiteral("stt_provider")), QStringLiteral("deepgram-nova3"));
        QVERIFY(query.allQueryItemValues(QStringLiteral("keyterms")).isEmpty());
        QCOMPARE(claudeVoiceKeytermsHeader({
                     QStringLiteral(" Deepgram   Nova 3 "),
                     QStringLiteral("Speecher"),
                     QStringLiteral("speecher"),
                     QString::fromUtf8("café"),
                 }),
                 QByteArrayLiteral("Deepgram Nova 3,Speecher,caf"));
        QCOMPARE(claudeVoiceKeytermsHeader({QString(1100, QLatin1Char('a'))}).size(), 1024);
    }

    void claudeVoiceEventsUseOnlyTheObservedSchema()
    {
        ClaudeVoiceEvent event = parseClaudeVoiceEvent(
            QStringLiteral(R"({"type":"TranscriptInterim","data":"working"})"));
        QCOMPARE(event.kind, ClaudeVoiceEventKind::Working);
        QCOMPARE(event.data, QStringLiteral("working"));

        event = parseClaudeVoiceEvent(
            QStringLiteral(R"({"type":"TranscriptText","data":"replacement"})"));
        QCOMPARE(event.kind, ClaudeVoiceEventKind::Working);
        QCOMPARE(event.data, QStringLiteral("replacement"));

        event = parseClaudeVoiceEvent(
            QStringLiteral(R"({"type":"TranscriptEndpoint","data":"endpoint text"})"));
        QCOMPARE(event.kind, ClaudeVoiceEventKind::Endpoint);
        QCOMPARE(event.data, QStringLiteral("endpoint text"));

        event = parseClaudeVoiceEvent(
            QStringLiteral(R"({"type":"TranscriptError","error":{"code":"stream_failed"}})"));
        QCOMPARE(event.kind, ClaudeVoiceEventKind::TranscriptError);
        QVERIFY(event.errorSummary.contains(QStringLiteral("stream_failed")));

        event = parseClaudeVoiceEvent(
            QStringLiteral(R"({"type":"unrelated","nested":{"text":"must not become a transcript"}})"));
        QCOMPARE(event.kind, ClaudeVoiceEventKind::Unknown);
        QVERIFY(event.data.isEmpty());
    }

#ifdef SPEECHER_WITH_QT_WEBSOCKETS
    void liveClaudeVoiceProvider()
    {
        const QString pcmPath = qEnvironmentVariable("SPEECHER_TEST_LIVE_CLAUDE_PCM");
        if (pcmPath.isEmpty()) {
            QSKIP("Live Claude Voice check is opt-in");
        }

        const SpeechSettings speech = SettingsStore().snapshot().speech;
        const ClaudeCredentialResult credentials = ClaudeCredentials::load(
            speech.claudeCredentialsPath,
            true);
        QVERIFY2(credentials.ok, qPrintable(credentials.error));

        QFile pcmFile(pcmPath);
        QVERIFY2(pcmFile.open(QIODevice::ReadOnly), "Could not read live Claude PCM input");
        const QByteArray pcm = pcmFile.readAll();
        QVERIFY2(pcm.size() >= 3200 && pcm.size() % 2 == 0,
                 "Live Claude PCM must be mono 16 kHz signed 16-bit raw audio");

        QUrl voiceUrl(speech.claudeEndpointBase);
        voiceUrl.setScheme(voiceUrl.scheme() == QStringLiteral("http")
                               ? QStringLiteral("ws")
                               : QStringLiteral("wss"));
        voiceUrl.setPath(speech.claudeVoicePath);

        {
            ClaudeVoiceClient client;
            QSignalSpy final(&client, &ClaudeVoiceClient::finalTranscript);
            QSignalSpy completed(&client, &ClaudeVoiceClient::completed);
            QSignalSpy failed(&client, &ClaudeVoiceClient::failed);
            QTimer sender;
            sender.setInterval(100);
            qsizetype offset = 0;
            connect(&client, &ClaudeVoiceClient::connected, &sender,
                    qOverload<>(&QTimer::start));
            connect(&sender, &QTimer::timeout, &client,
                    [&client, &sender, &pcm, &offset] {
                        constexpr qsizetype bytesPerTick = 3200;
                        const QByteArray chunk = pcm.mid(offset, bytesPerTick);
                        offset += chunk.size();
                        if (!chunk.isEmpty()) {
                            client.sendAudio(chunk);
                        }
                        if (offset >= pcm.size()) {
                            sender.stop();
                            client.stop();
                        }
                    });

            client.start(voiceUrl, credentials.accessToken, speech.vocabulary);
            const int audioDurationMs = qRound(pcm.size() * 1000.0 / 32000.0);
            QTRY_VERIFY_WITH_TIMEOUT(!completed.isEmpty() || !failed.isEmpty(),
                                     audioDurationMs + 10000);
            const QString failure = failed.isEmpty()
                ? QString()
                : failed.first().first().toString();
            QVERIFY2(failed.isEmpty(), qPrintable(failure));
            QCOMPARE(completed.count(), 1);
            QVERIFY(!final.isEmpty());
            QVERIFY(!final.last().first().toString().trimmed().isEmpty());
        }

        {
            ClaudeVoiceClient client;
            QSignalSpy completed(&client, &ClaudeVoiceClient::completed);
            QSignalSpy failed(&client, &ClaudeVoiceClient::failed);
            connect(&client, &ClaudeVoiceClient::connected, &client,
                    &ClaudeVoiceClient::stop);
            client.start(voiceUrl, credentials.accessToken, speech.vocabulary);
            QTRY_VERIFY_WITH_TIMEOUT(!completed.isEmpty() || !failed.isEmpty(), 8000);
            if (!failed.isEmpty()) {
                QVERIFY(!failed.first().at(2).toString().isEmpty());
            }
        }
    }

    void claudeVoiceClientHandlesPauseEndpointsAndFinalization()
    {
        QWebSocketServer server(QStringLiteral("speecher-test"), QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost));

        ClaudeVoiceClient client;
        QSignalSpy connected(&client, &ClaudeVoiceClient::connected);
        QSignalSpy partial(&client, &ClaudeVoiceClient::partialTranscript);
        QSignalSpy final(&client, &ClaudeVoiceClient::finalTranscript);
        QSignalSpy completed(&client, &ClaudeVoiceClient::completed);
        QSignalSpy failed(&client, &ClaudeVoiceClient::failed);

        client.start(
            QUrl(QStringLiteral("ws://127.0.0.1:%1/voice").arg(server.serverPort())),
            QStringLiteral("test-token"),
            {QStringLiteral("Speecher")});
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
        std::unique_ptr<QWebSocket> socket(server.nextPendingConnection());
        QVERIFY(socket);
        QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 1000);

        socket->sendTextMessage(QStringLiteral(
            R"({"type":"TranscriptInterim","data":"first phrase"})"));
        QTRY_COMPARE_WITH_TIMEOUT(partial.count(), 1, 1000);
        socket->sendTextMessage(QStringLiteral(
            R"({"type":"TranscriptEndpoint","data":"first phrase"})"));
        QTRY_COMPARE_WITH_TIMEOUT(final.count(), 1, 1000);
        QCOMPARE(completed.count(), 0);

        socket->sendTextMessage(QStringLiteral(
            R"({"type":"TranscriptText","data":"second phrase"})"));
        QTRY_COMPARE_WITH_TIMEOUT(partial.count(), 2, 1000);

        QSignalSpy clientMessages(socket.get(), &QWebSocket::textMessageReceived);
        client.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!clientMessages.isEmpty(), 1000);
        QCOMPARE(clientMessages.last().first().toString(),
                 QStringLiteral("{\"type\":\"CloseStream\"}"));
        socket->sendTextMessage(QStringLiteral(
            R"({"type":"TranscriptEndpoint","data":"second phrase"})"));

        QTRY_COMPARE_WITH_TIMEOUT(final.count(), 2, 1000);
        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 1000);
        QCOMPARE(failed.count(), 0);
    }

    void claudeVoiceClientClassifiesAuthenticationRefusal()
    {
        QWebSocketServer server(QStringLiteral("speecher-test"), QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost));

        ClaudeVoiceClient client;
        QSignalSpy failed(&client, &ClaudeVoiceClient::failed);
        client.start(
            QUrl(QStringLiteral("ws://127.0.0.1:%1/voice").arg(server.serverPort())),
            QStringLiteral("invalid-token"),
            {});

        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
        std::unique_ptr<QWebSocket> socket(server.nextPendingConnection());
        QVERIFY(socket);
        socket->sendTextMessage(QStringLiteral(
            R"({"type":"error","error":{"code":"401","message":"unauthorized"}})"));

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 1000);
        QCOMPARE(failed.first().at(1).toBool(), false);
        QCOMPARE(failed.first().at(2).toString(), QStringLiteral("authentication"));
    }

#endif
};

int runClaudeVoiceTests(int argc, char **argv)
{
    ClaudeVoiceTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_claude_voice.moc"
