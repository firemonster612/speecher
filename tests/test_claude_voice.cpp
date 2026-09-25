#include "common/test_suites.h"
#include "common/test_auth.h"
#include "common/test_doubles.h"

#include "core/AppSettings.h"
#include "core/SettingsStore.h"
#include "core/settings/SettingsKeys.h"
#include "dictation/DictationSession.h"
#include "providers/ClaudeCredentials.h"
#include "providers/ClaudeSpeechTranscriber.h"
#include "providers/ClaudeVoiceClient.h"
#include "providers/ClaudeVoiceProtocol.h"

#include <QFile>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTimer>
#include <QtTest>
#ifdef SPEECHER_WITH_QT_WEBSOCKETS
#include <QWebSocket>
#include <QWebSocketServer>
#endif

#include <memory>

using namespace speecher;
using namespace speecher::test;


class ClaudeVoiceTests : public QObject {
    Q_OBJECT

private slots:
    void claudeVoiceStreamQueryMatchesClaudeCode()
    {
        const QUrlQuery query = claudeVoiceStreamQuery();

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
                 QByteArray("Deepgram Nova 3,Speecher,caf\xe9", 29));
        QCOMPARE(claudeVoiceKeytermsHeader({QString(1100, QLatin1Char('a')),
                                            QStringLiteral("Qt")}),
                 QByteArrayLiteral("Qt"));
        QCOMPARE(claudeVoiceKeytermsHeader({QString::fromUtf8("日本語"),
                                            QStringLiteral("Speecher")}),
                 QByteArrayLiteral("Speecher"));
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
    void claudeVoiceClientBoundsConnectionAndPendingAudio()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));

        ClaudeVoiceClient deadlineClient(nullptr, 25);
        QSignalSpy deadlineFailure(&deadlineClient, &ClaudeVoiceClient::failed);
        deadlineClient.start(
            QUrl(QStringLiteral("ws://127.0.0.1:%1/voice").arg(server.serverPort())),
            QStringLiteral("test-token"),
            {});
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
        QTRY_COMPARE_WITH_TIMEOUT(deadlineFailure.count(), 1, 1000);
        QCOMPARE(deadlineFailure.first().at(2).toString(), QStringLiteral("connect"));

        ClaudeVoiceClient bufferedClient(nullptr, 1000);
        QSignalSpy bufferFailure(&bufferedClient, &ClaudeVoiceClient::failed);
        bufferedClient.start(
            QUrl(QStringLiteral("ws://127.0.0.1:%1/voice").arg(server.serverPort())),
            QStringLiteral("test-token"),
            {});
        bufferedClient.sendAudio(QByteArray(4 * 1024 * 1024 + 1, '\0'));
        QCOMPARE(bufferFailure.count(), 1);
        QCOMPARE(bufferFailure.first().at(2).toString(), QStringLiteral("connect"));
    }

    void claudeVoiceClientEndsALiveStreamOnAnyServerEnd_data()
    {
        QTest::addColumn<bool>("dropped");
        QTest::addColumn<bool>("finishing");
        QTest::addColumn<QString>("failedPhase");
        QTest::newRow("clean close while streaming") << false << false << QString();
        QTest::newRow("dropped while streaming") << true << false << QString();
        QTest::newRow("dropped while finalizing") << true << true << QStringLiteral("finalize");
    }

    // Qt cannot tell a server's clean close from a drop, so any end of a live
    // stream is the server ending it; once the client asked to finish, an end
    // without the final transcript is still a failure.
    void claudeVoiceClientEndsALiveStreamOnAnyServerEnd()
    {
        QFETCH(bool, dropped);
        QFETCH(bool, finishing);
        QFETCH(QString, failedPhase);
        QWebSocketServer server(QStringLiteral("speecher-test"), QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost));

        ClaudeVoiceClient client;
        QSignalSpy connected(&client, &ClaudeVoiceClient::connected);
        QSignalSpy completed(&client, &ClaudeVoiceClient::completed);
        QSignalSpy failed(&client, &ClaudeVoiceClient::failed);
        QSignalSpy closed(&client, &ClaudeVoiceClient::closed);
        client.start(
            QUrl(QStringLiteral("ws://127.0.0.1:%1/voice").arg(server.serverPort())),
            QStringLiteral("test-token"),
            {});
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
        std::unique_ptr<QWebSocket> socket(server.nextPendingConnection());
        QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 1000);
        if (finishing) {
            QSignalSpy serverMessages(socket.get(), &QWebSocket::textMessageReceived);
            client.stop();
            QTRY_VERIFY_WITH_TIMEOUT(serverMessages.contains(
                                         QVariantList{QStringLiteral("{\"type\":\"CloseStream\"}")}), 1000);
        }

        if (dropped) {
            socket->abort();
        } else {
            socket->close();
        }
        QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 1000);
        if (failedPhase.isEmpty()) {
            QCOMPARE(completed.count(), 1);
            QCOMPARE(failed.count(), 0);
        } else {
            QCOMPARE(completed.count(), 0);
            QCOMPARE(failed.count(), 1);
            QCOMPARE(failed.first().at(1).toBool(), true);
            QCOMPARE(failed.first().at(2).toString(), failedPhase);
        }
    }

    void claudeVoiceClientFailsWhenTheServerDropsItBeforeConnecting()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));

        ClaudeVoiceClient client;
        QSignalSpy completed(&client, &ClaudeVoiceClient::completed);
        QSignalSpy failed(&client, &ClaudeVoiceClient::failed);
        client.start(
            QUrl(QStringLiteral("ws://127.0.0.1:%1/voice").arg(server.serverPort())),
            QStringLiteral("test-token"),
            {});
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
        std::unique_ptr<QTcpSocket> socket(server.nextPendingConnection());
        socket->abort();

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 1000);
        QCOMPARE(failed.first().at(1).toBool(), true);
        QCOMPARE(failed.first().at(2).toString(), QStringLiteral("connect"));
        QCOMPARE(completed.count(), 0);
    }

    // Any end of a live voice stream, a clean close or a drop, is the server
    // ending it, not an error; Speecher rolls over to a new stream.
    void claudeStreamEndedByTheServerRollsOverWithoutLosingDictation()
    {
        QTemporaryDir credentials;
        QVERIFY(writeCliProxyAccount(credentials.path(), QStringLiteral("claude-a@example.com.json"),
                                     QStringLiteral("claude"), QStringLiteral("claude-token"),
                                     QDateTime::currentDateTimeUtc().addSecs(3600)));
        QWebSocketServer server(QStringLiteral("speecher-test"), QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const int stableAttemptMs = DictationSession::stableAttemptMs();
        DictationSession::setStableAttemptMs(0);
        const auto restore = qScopeGuard([stableAttemptMs] {
            DictationSession::setStableAttemptMs(stableAttemptMs);
        });

        QList<QWebSocket *> peers;
        QList<QStringList> audioByPeer;
        connect(&server, &QWebSocketServer::newConnection, this, [&] {
            QWebSocket *peer = server.nextPendingConnection();
            const qsizetype index = peers.size();
            peers.append(peer);
            audioByPeer.append(QStringList());
            connect(peer, &QWebSocket::binaryMessageReceived, this, [&audioByPeer, index](const QByteArray &pcm) {
                audioByPeer[index].append(QString::fromLatin1(pcm));
            });
        });

        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("none"));
        settings.setSpeechProvider(QStringLiteral("claude"));
        settings.setAnthropicAuthMode(QStringLiteral("cliproxy"));
        settings.setCliproxyOauthDir(credentials.path());
        settings.raw().setValue(SettingsKeys::ClaudeEndpointBase,
                                QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
        settings.raw().setValue(SettingsKeys::ClaudeVoicePath, QStringLiteral("/voice"));
        FakeAudioInput audio;
        FakeMediaController media;
        FakeDelivery delivery;
        ProviderRegistry registry;
        ClaudeSpeechTranscriber *transcriber = nullptr;
        registry.registerSpeechProvider({QStringLiteral("claude"), QStringLiteral("Claude Voice")},
                                        [&transcriber](QObject *parent) {
                                            transcriber = new ClaudeSpeechTranscriber(parent);
                                            return transcriber;
                                        });
        DictationSession session(&settings, &audio, &media, &delivery, &registry);
        QSignalSpy errors(&session, &DictationSession::popupErrorRequested);
        QSignalSpy previews(&session, &DictationSession::previewChanged);

        session.startListening();
        QTRY_COMPARE_WITH_TIMEOUT(session.state(), DictationState::Listening, 1000);
        QVERIFY(transcriber);
        QSignalSpy failures(transcriber, &SpeechTranscriber::failed);

        // More rollovers than the error budget allows reconnects.
        constexpr int rollovers = 3;
        for (int round = 0; round <= rollovers; ++round) {
            QTRY_COMPARE_WITH_TIMEOUT(peers.size(), round + 1, 1000);
            audio.pushAudio(QStringLiteral("speech %1").arg(round).toLatin1());
            QTRY_COMPARE_WITH_TIMEOUT(audioByPeer[round],
                                      QStringList({QStringLiteral("speech %1").arg(round)}), 1000);
            peers[round]->sendTextMessage(QStringLiteral(
                R"({"type":"TranscriptEndpoint","data":"part %1"})").arg(round));
            if (round == rollovers) {
                break;
            }
            // The current utterance never reached an endpoint: the rollover keeps it.
            peers[round]->sendTextMessage(QStringLiteral(
                R"({"type":"TranscriptInterim","data":"tail %1"})").arg(round));
            QTRY_VERIFY_WITH_TIMEOUT(previews.last().first().toString().endsWith(
                                         QStringLiteral("tail %1").arg(round)), 1000);
            if (round % 2 == 0) {
                peers[round]->close();
            } else {
                peers[round]->abort();
            }
        }
        QTRY_VERIFY_WITH_TIMEOUT(previews.last().first().toString().endsWith(QStringLiteral("part 3")), 1000);
        QCOMPARE(session.state(), DictationState::Listening);
        QVERIFY(audio.isActive());

        QSignalSpy clientMessages(peers.last(), &QWebSocket::textMessageReceived);
        session.stopListening();
        QTRY_VERIFY_WITH_TIMEOUT(clientMessages.contains(QVariantList{QStringLiteral("{\"type\":\"CloseStream\"}")}), 1000);
        peers.last()->sendTextMessage(QStringLiteral(R"({"type":"TranscriptEndpoint","data":""})"));

        QTRY_COMPARE_WITH_TIMEOUT(delivery.calls, 1, 1000);
        QCOMPARE(delivery.lastText,
                 QStringLiteral("part 0 tail 0 part 1 tail 1 part 2 tail 2 part 3"));
        QCOMPARE(failures.count(), 0);
        QCOMPARE(errors.count(), 0);
        QVERIFY2(!session.lastMessage().contains(QStringLiteral("may be missing")),
                 qPrintable(session.lastMessage()));
        qDeleteAll(peers);
    }

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
