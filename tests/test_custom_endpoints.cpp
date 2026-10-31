#include "common/test_prelude.h"
#include "common/test_secrets.h"
#include "common/test_http.h"
#include "common/test_doubles.h"
#include "core/EndpointSettings.h"
#include "core/settings/SettingsKeys.h"
#include "providers/ChatCompletionsRefiner.h"
#include "providers/CustomEndpoints.h"
#include "providers/EndpointSpeechTranscriber.h"
#include "providers/EndpointTranscriptRefiner.h"
#include "providers/LocalRunner.h"
#ifdef Q_OS_LINUX
#include "app/SpeecherRemoval.h"
#endif

#include <QDir>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QProcess>
#include <QScopeGuard>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>

#include <optional>

#ifndef Q_OS_WIN
#include <sys/stat.h>
#include <unistd.h>
#endif

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
    transcriber.startAttempt(attempt, settings, {});
    transcriber.sendAudio(attempt, QByteArray(640, '\0'));
    transcriber.finishInput(attempt);
}

QByteArray headersOf(const QByteArray &request)
{
    return request.left(request.indexOf("\r\n\r\n")).toLower();
}


// Set by the ctest that runs this suite against a real keyring.
bool privateKeyring()
{
    return qEnvironmentVariableIntValue("SPEECHER_TEST_PRIVATE_KEYRING") == 1;
}

const char *const peerSecretVariable = "SPEECHER_TEST_PEER_SECRET";
const char *const peerRestartsVariable = "SPEECHER_TEST_PEER_RESTARTS";
const QByteArray peerReadMarker = "peer read: ";

// Where this test process keeps its settings files (tests/test_main.cpp).
QString preferencesPath(SettingsStore &settings)
{
    QDir preferences = QFileInfo(settings.raw().fileName()).absoluteDir();
    preferences.cdUp();
    return preferences.path();
}

#ifndef Q_OS_WIN
// The permissions that let anyone but the owner in.
constexpr QFile::Permissions openToOthers = QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup
    | QFile::ReadOther | QFile::WriteOther | QFile::ExeOther;
#endif

// Deleting the settings moves every QSettings made afterwards to a throwaway
// folder; the guard puts them back here for the tests that come after.
auto keepSettingsHere(SettingsStore &settings)
{
    return qScopeGuard([preferences = preferencesPath(settings)] {
        QSettings::setPath(QSettings::defaultFormat(), QSettings::UserScope, preferences);
        QSettings::setPath(QSettings::defaultFormat(), QSettings::SystemScope, preferences);
    });
}

#ifdef Q_OS_LINUX
// A removal of Speecher whose keyring and settings steps act on this store;
// the other steps find nothing to remove.
SpeecherRemoval::Steps removalSteps(SettingsStore &settings)
{
    SpeecherRemoval::Steps steps;
    steps.removeDesktopIntegration = [] { return DesktopIntegrationRemoval{}; };
    steps.removeShortcutRegistration = [](QString *) { return true; };
    steps.virtualKeyboardInstalled = [] { return false; };
    steps.keyHelperInstalled = [] { return false; };
    steps.deleteSecrets = [&settings](SpeecherRemoval::Steps::SecretsDeleted done) {
        settings.secrets()->deleteKeyringSecrets(std::move(done));
    };
    steps.settingsHideKeys = [&settings] { return !settings.secrets()->deletionMarkers().isEmpty(); };
    steps.deleteSettings = [&settings](QString *path) { return settings.deleteAll(path); };
    steps.deleteSettingsButMarkers = [&settings](QString *path) { return settings.deleteAllButDeletionMarkers(path); };
    return steps;
}
#endif

// Compares what another Speecher process reads for the secret, from this
// one's settings file and keyring, with expected: a peer of this binary
// running printWhatAPeerReads. It saves nothing first: a key a save said was
// stored is in the files already. With restart, the peer first runs
// startup's migrations, as Speecher starting again does. Windows test
// processes each keep their settings in a registry key of their own, so there
// it checks nothing.
void comparePeerRead(SettingsStore &settings, SecretStore::Secret secret, const QString &expected,
                     bool restart = false)
{
#ifdef Q_OS_WIN
    Q_UNUSED(settings);
    Q_UNUSED(secret);
    Q_UNUSED(expected);
    Q_UNUSED(restart);
#else
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("SPEECHER_TEST_PREFERENCES_DIR"), preferencesPath(settings));
    environment.insert(QString::fromLatin1(peerSecretVariable), QString::number(int(secret)));
    if (restart) {
        environment.insert(QString::fromLatin1(peerRestartsVariable), QStringLiteral("1"));
    }
    QProcess peer;
    peer.setProcessEnvironment(environment);
    peer.setProcessChannelMode(QProcess::MergedChannels);
    peer.start(QCoreApplication::applicationFilePath(),
               {QStringLiteral("--suite"), QStringLiteral("custom_endpoints"), QStringLiteral("printWhatAPeerReads")});
    QVERIFY(peer.waitForFinished());
    const QByteArray output = peer.readAll();
    const qsizetype start = output.indexOf(peerReadMarker + '[');
    QVERIFY2(peer.exitCode() == 0 && start >= 0, output.constData());
    const qsizetype value = start + peerReadMarker.size() + 1;
    QCOMPARE(QString::fromUtf8(output.mid(value, output.indexOf(']', value) - value)), expected);
#endif
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
        transcriber.startAttempt(1, settings, {});
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
        // A server that answers with anything but the transcript failed.
        QTest::newRow("http-200-html") << httpResponse("200 OK", "text/html", "<html>Sign in</html>")
                                       << "held no transcript" << QString() << ProviderFailureKind::InvalidResult;
        QTest::newRow("json-without-text") << httpResponse("200 OK", "application/json", "{\"transcript\":\"Hi\"}")
                                           << "held no transcript" << QString() << ProviderFailureKind::InvalidResult;
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
        QCOMPARE(failure.phase, SpeechPhase::Finalize);
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
        QCOMPARE(failure.phase, SpeechPhase::Finalize);
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
        transcriber.startAttempt(1, settings, {.cutIntoUtterances = true});
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
        transcriber.startAttempt(1, settings, {.cutIntoUtterances = true});
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
        refiner.refine(QStringLiteral("hello there"), {QStringLiteral("Qt")}, QStringLiteral("key"),
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
        refiner.refine(QStringLiteral("um hello there"), vocabulary, {},
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
            refiner.refine(QStringLiteral("fine"), {}, {}, server.origin() + QStringLiteral("/v1"),
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
            refiner.refine(QStringLiteral("x"), {}, {}, server.origin() + QStringLiteral("/v1"),
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
        EndpointTranscriptRefiner refiner(QStringLiteral("Custom Endpoint"));
        QSignalSpy completed(&refiner, &TranscriptRefiner::completed);
        QSignalSpy failed(&refiner, &TranscriptRefiner::failed);
        RefinementContext context;

        refiner.refine(QStringLiteral("done"), {}, context, settings, {});

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
        refiner.refine(QStringLiteral("x"), {}, context, settings, {});
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
        EndpointTranscriptRefiner refiner(QStringLiteral("Custom Endpoint"));
        QSignalSpy failed(&refiner, &TranscriptRefiner::failed);
        refiner.refine(QStringLiteral("x"), {}, {}, settings, {});
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

    // Changing a Custom endpoint's server leaves the CLI Proxy key behind,
    // whether the endpoint uses it unread or holds the copy leaving the
    // preset made.
    void anotherServerDoesNotGetTheProxyKey_data()
    {
        QTest::addColumn<bool>("unread");
        QTest::newRow("read") << false;
        QTest::newRow("unread") << true;
    }

    void anotherServerDoesNotGetTheProxyKey()
    {
        QFETCH(bool, unread);
        AppSettings settings;
        settings.refinement.cliproxyBaseUrl = QStringLiteral("http://127.0.0.1:8317");
        settings.refinement.endpoint.preset = QStringLiteral("cliproxy");
        if (unread) {
            settings.unreadSecretKeys = {SecretStore::settingsKey(SecretStore::Secret::CliproxyApiKey)};
        } else {
            settings.refinement.cliproxyApiKey = QStringLiteral("proxy-key");
        }
        editRefinementEndpoint(settings, {.preset = QString()});

        editRefinementEndpoint(settings, {.baseUrl = QStringLiteral("http://other.example/v1")});

        // As a request reads it, the proxy key read by then.
        settings.refinement.cliproxyApiKey = QStringLiteral("proxy-key");
        QCOMPARE(resolvedRefinementEndpoint(settings.refinement).apiKey, QString());
    }

    // So does the copy leaving the preset made, after a restart that read
    // neither key yet.
    void anotherServerDoesNotGetTheCopiedProxyKeyAfterARestart()
    {
        const auto restore = qScopeGuard([] { removedSecrets(SettingsStore().secrets()); });
        {
            SettingsStore settings;
            settings.raw().clear();
            settings.edit([](AppSettings &draft) {
                draft.refinement.cliproxyBaseUrl = QStringLiteral("http://127.0.0.1:8317");
                draft.refinement.cliproxyApiKey = QStringLiteral("proxy-key");
                draft.refinement.endpoint.preset = QStringLiteral("cliproxy");
                return true;
            });
            settings.edit([](AppSettings &draft) {
                editRefinementEndpoint(draft, {.preset = QString()});
                return true;
            });
        }

        SettingsStore restarted;
        restarted.edit([](AppSettings &draft) {
            editRefinementEndpoint(draft, {.baseUrl = QStringLiteral("http://other.example/v1")});
            return true;
        });

        QCOMPARE(resolvedRefinementEndpoint(SettingsStore().snapshot().refinement).apiKey, QString());
    }

    // A Custom endpoint's key stays only while its server does: a new path
    // keeps it, a new scheme, host or port drops it, also a plain copy of the
    // proxy's key that an earlier build saved.
    void aKeyStaysOnlyOnItsServer_data()
    {
        QTest::addColumn<QString>("key");
        QTest::addColumn<QString>("url");
        QTest::addColumn<QString>("sent");
        const QString own = QStringLiteral("own-key");
        QTest::newRow("another path") << own << QStringLiteral("http://localhost:11434/api/v1") << own;
        QTest::newRow("another port") << own << QStringLiteral("http://localhost:11435/v1") << QString();
        QTest::newRow("another scheme") << own << QStringLiteral("https://localhost:11434/v1") << QString();
        QTest::newRow("copied proxy key, another host")
            << QStringLiteral("proxy-key") << QStringLiteral("http://other.example/v1") << QString();
    }

    void aKeyStaysOnlyOnItsServer()
    {
        QFETCH(QString, key);
        QFETCH(QString, url);
        QFETCH(QString, sent);
        AppSettings settings;
        settings.refinement.cliproxyApiKey = QStringLiteral("proxy-key");
        settings.refinement.endpoint.baseUrl = QStringLiteral("http://localhost:11434/v1");
        settings.refinement.endpoint.apiKey = key;

        editRefinementEndpoint(settings, {.baseUrl = url});

        QCOMPARE(resolvedRefinementEndpoint(settings.refinement).apiKey, sent);
    }

    // An earlier build marked a key typed on the proxy's server as unused
    // (useCliproxyKey); it is sent now, and the next save drops the mark.
    void aTypedKeyAnEarlierBuildMarkedUnusedIsSent()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.raw().setValue(SettingsKeys::CliproxyBaseUrl, QStringLiteral("http://127.0.0.1:8317"));
        settings.raw().setValue(SettingsKeys::RefinementEndpointBaseUrl, QStringLiteral("http://127.0.0.1:8317/v1"));
        settings.raw().setValue(SettingsKeys::RefinementEndpointUseCliproxyKey, true);
        settings.raw().setValue(SecretStore::settingsKey(SecretStore::Secret::CliproxyApiKey), QStringLiteral("proxy-key"));
        settings.raw().setValue(SecretStore::settingsKey(SecretStore::Secret::RefinementEndpointKey),
                                QStringLiteral("typed-key"));

        QCOMPARE(resolvedRefinementEndpoint(settings.snapshot().refinement).apiKey, QStringLiteral("typed-key"));

        settings.edit([](AppSettings &draft) {
            draft.refinement.endpoint.model = QStringLiteral("model");
            return true;
        });
        QVERIFY(!settings.raw().contains(SettingsKeys::RefinementEndpointUseCliproxyKey));
    }

    // Startup reads only the key the endpoint will send: its own, and the
    // proxy's only when it has none and is on the proxy's server. A proxy key
    // left unread then can't stop the endpoint's own. With the endpoint's own
    // key unread too, that key is read first.
    void onlyTheKeyTheEndpointSendsIsRead_data()
    {
        QTest::addColumn<QString>("ownKey");
        QTest::addColumn<QString>("url");
        QTest::addColumn<bool>("ownUnread");
        QTest::addColumn<bool>("readsAKey");
        const QString proxy = QStringLiteral("http://127.0.0.1:8317/v1");
        const QString other = QStringLiteral("http://server.example/v1");
        QTest::newRow("own key, the proxy's server") << QStringLiteral("own-key") << proxy << false << false;
        QTest::newRow("own key, after the proxy moved") << QStringLiteral("own-key") << other << false << false;
        QTest::newRow("no key, another server") << QString() << other << false << false;
        QTest::newRow("no key, the proxy's server") << QString() << proxy << false << true;
        QTest::newRow("both unread, the proxy's server") << QString() << proxy << true << true;
    }

    void onlyTheKeyTheEndpointSendsIsRead()
    {
        QFETCH(QString, ownKey);
        QFETCH(QString, url);
        QFETCH(bool, ownUnread);
        QFETCH(bool, readsAKey);
        AppSettings settings;
        settings.refinement.cliproxyBaseUrl = QStringLiteral("http://127.0.0.1:8317");
        settings.refinement.endpoint.baseUrl = url;
        settings.refinement.endpoint.apiKey = ownKey;
        settings.unreadSecretKeys = {SecretStore::settingsKey(SecretStore::Secret::CliproxyApiKey)};
        if (ownUnread) {
            settings.unreadSecretKeys.append(SecretStore::settingsKey(SecretStore::Secret::RefinementEndpointKey));
        }

        QCOMPARE(SettingsStore::hasUnreadProviderSecrets(settings, ProviderRole::Refinement, QStringLiteral("endpoint")),
                 readsAKey);
    }

    // An endpoint left at the proxy's server stops getting the proxy's key
    // once the proxy moves to another one.
    void anEndpointLeftAtAMovedProxySendsNoKey()
    {
        AppSettings settings;
        settings.refinement.cliproxyBaseUrl = QStringLiteral("http://127.0.0.1:8317");
        settings.refinement.cliproxyApiKey = QStringLiteral("proxy-key");
        settings.refinement.endpoint.preset = QStringLiteral("cliproxy");
        editRefinementEndpoint(settings, {.preset = QString()});
        QCOMPARE(resolvedRefinementEndpoint(settings.refinement).apiKey, QStringLiteral("proxy-key"));

        settings.refinement.cliproxyBaseUrl = QStringLiteral("http://proxy-b.example:8317");
        settings.refinement.cliproxyApiKey = QStringLiteral("key-b");

        QCOMPARE(resolvedRefinementEndpoint(settings.refinement).apiKey, QString());
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
        QSignalSpy finished(settings.secrets(), &SecretStore::saveFinished);
        settings.applySnapshot(draft);
        QTRY_COMPARE(finished.count(), 2);

        SettingsStore reopened;
        readSecret(reopened.secrets(), SecretStore::Secret::SpeechEndpointKey);
        readSecret(reopened.secrets(), SecretStore::Secret::RefinementEndpointKey);
        const AppSettings loaded = reopened.snapshot();
        QCOMPARE(loaded.speech.endpoint.apiKey, QStringLiteral("speech-secret"));
        QCOMPARE(loaded.refinement.endpoint.apiKey, QStringLiteral("refinement-secret"));
        const bool inKeyring = reopened.raw().value(SettingsKeys::SecretsInKeyring).toStringList()
                                   .contains(QStringLiteral("speech-endpoint-key"));
        QCOMPARE(reopened.raw().contains(SettingsKeys::SpeechEndpointApiKey), !inKeyring);
        qInfo() << "endpoint keys stored in" << (inKeyring ? "the keyring" : "the settings file");
        if (privateKeyring()) {
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
        QTRY_COMPARE(SettingsStore().snapshot().speech.endpoint.apiKey, QString());
        QTRY_VERIFY(!reopened.raw().contains(SettingsKeys::SecretsInKeyring));
        QVERIFY(!reopened.raw().contains(SettingsKeys::SpeechEndpointApiKey));
        QVERIFY(!reopened.raw().contains(SettingsKeys::SecretsInKeyring));
    }

#ifndef Q_OS_WIN
    // The settings file and its folder are the owner's alone before a key no
    // keyring takes goes in, under a umask that opens new files to everyone,
    // and stay so when the store writes its last changes as it closes.
    void theSettingsFileHoldingAKeyIsTheOwnersAlone()
    {
        qputenv("SPEECHER_TEST_KEYRING_WRITE_FAILURE", "1");
        const auto restore = qScopeGuard([] { qunsetenv("SPEECHER_TEST_KEYRING_WRITE_FAILURE"); });
        const mode_t umaskBefore = umask(0);
        const auto restoreUmask = qScopeGuard([umaskBefore] { umask(umaskBefore); });
        const auto proxy = SecretStore::Secret::CliproxyApiKey;
        QString file;
        {
            SettingsStore settings;
            settings.raw().clear();
            file = settings.raw().fileName();
            QFile::remove(file);
            QFile::setPermissions(QFileInfo(file).absolutePath(), openToOthers | QFile::ReadOwner
                                      | QFile::WriteOwner | QFile::ExeOwner);

            QVERIFY(savedSecret(settings.secrets(), proxy, QStringLiteral("file-key")));

            QVERIFY(settings.raw().contains(SecretStore::settingsKey(proxy)));
            settings.raw().setValue(QStringLiteral("test/closing"), true);
        }
        QVERIFY(QSettings(file, QSettings::IniFormat).value(QStringLiteral("test/closing")).toBool());
        QCOMPARE((QFile::permissions(file) & openToOthers).toInt(), 0);
        QCOMPARE((QFile::permissions(QFileInfo(file).absolutePath()) & openToOthers).toInt(), 0);
    }

    // Migration copies the older settings' keys only into settings of the
    // owner's alone, under a umask that opens new files to everyone, and
    // closes the older file the keys came from too.
    void migratedKeysLandInSettingsOfTheOwnersAlone()
    {
        const mode_t umaskBefore = umask(0);
        const auto restoreUmask = qScopeGuard([umaskBefore] { umask(umaskBefore); });
        const QString key = SecretStore::settingsKey(SecretStore::Secret::CliproxyApiKey);
        QString file;
        {
            SettingsStore settings;
            settings.raw().clear();
            file = settings.raw().fileName();
        }
        QFile::remove(file);
        const QString folder = QFileInfo(file).absolutePath();
        QFile::setPermissions(folder, openToOthers | QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QString olderFile;
        {
            QSettings older(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("local.speecher"),
                            SettingsKeys::application());
            older.setValue(key, QStringLiteral("older-key"));
            older.sync();
            olderFile = older.fileName();
        }
        const auto removeOlder = qScopeGuard([olderFile] {
            QFile::remove(olderFile);
            QDir().rmdir(QFileInfo(olderFile).absolutePath());
        });

        migrateSavedSettings();

        QCOMPARE(QSettings(file, QSettings::IniFormat).value(key).toString(), QStringLiteral("older-key"));
        for (const QString &path : {file, folder, olderFile, QFileInfo(olderFile).absolutePath()}) {
            QCOMPARE((QFile::permissions(path) & openToOthers).toInt(), 0);
        }
    }
#endif

#ifdef Q_OS_LINUX
    // Settings in a folder another user owns can't be made the owner's
    // alone, so nothing is made there for a key to go in.
    void settingsInAnotherUsersFolderAreNotMadePrivate()
    {
        if (getuid() == 0) {
            QSKIP("Root can change any folder");
        }
        const QSettings settings(QStringLiteral("/speecher-test.ini"), QSettings::IniFormat);
        QString error;

        QVERIFY(!makeSettingsPrivate(settings, &error));

        QVERIFY2(error.contains(QStringLiteral("can't be kept from other users")), qPrintable(error));
        QVERIFY(!QFile::exists(settings.fileName()));
    }
#endif

    // Saving an empty key clears it, whether or not this store read it.
    void anEmptySaveClearsAKeyNeverRead()
    {
        qputenv("SPEECHER_TEST_KEYRING_WRITE_FAILURE", "1");
        const auto restore = qScopeGuard([] { qunsetenv("SPEECHER_TEST_KEYRING_WRITE_FAILURE"); });
        SettingsStore saved;
        saved.raw().clear();
        QVERIFY(savedSecret(saved.secrets(), SecretStore::Secret::CliproxyApiKey, QStringLiteral("old-key")));
        QCOMPARE(readSecret(SettingsStore().secrets(), SecretStore::Secret::CliproxyApiKey), QStringLiteral("old-key"));

        SettingsStore reopened;
        QVERIFY(!reopened.secrets()->isSecretKnown(SecretStore::Secret::CliproxyApiKey));
        QVERIFY(savedSecret(reopened.secrets(), SecretStore::Secret::CliproxyApiKey, {}));
        QCOMPARE(readSecret(SettingsStore().secrets(), SecretStore::Secret::CliproxyApiKey), QString());
    }

    // A save asked for while another write of the same key waits on the
    // keyring waits its turn. The save asked for last is the one kept, by
    // this store, a fresh one and another process, whatever the cache held
    // then, and the only one saveFinished answers. With a keyring, the first
    // save waits on a real write; without one, on a refused write that falls
    // back to the settings file.
    void theLastSaveOfAKeyWins_data()
    {
        QTest::addColumn<QString>("before");
        QTest::addColumn<QString>("nested");
        QTest::addColumn<bool>("nestedRefused");
        QTest::newRow("newer-key") << QStringLiteral("old-key") << QStringLiteral("nested-key") << false;
        QTest::newRow("back-to-the-cached-key") << QStringLiteral("old-key") << QStringLiteral("old-key") << false;
        QTest::newRow("cleared-first-key") << QString() << QString() << false;
        // With a keyring, the first save reaches it and the nested one is kept
        // in the settings file.
        QTest::newRow("nested-refused") << QString() << QStringLiteral("nested-key") << true;
    }

    void theLastSaveOfAKeyWins()
    {
        QFETCH(QString, before);
        QFETCH(QString, nested);
        QFETCH(bool, nestedRefused);
        const auto proxy = SecretStore::Secret::CliproxyApiKey;
        SettingsStore settings;
        settings.raw().clear();
        SecretStore *secrets = settings.secrets();
        const auto restore = qScopeGuard([secrets, proxy] {
            qunsetenv("SPEECHER_TEST_KEYRING_WRITE_FAILURE");
            savedSecret(secrets, proxy, {});
        });
        if (before.isEmpty()) {
            QCOMPARE(secrets->secret(proxy), QString());
        } else {
            QVERIFY(savedSecret(secrets, proxy, before));
        }
        QSignalSpy finished(secrets, &SecretStore::saveFinished);

        secrets->saveSecret(proxy, QStringLiteral("outer-key"));
        if (nestedRefused) {
            qputenv("SPEECHER_TEST_KEYRING_WRITE_FAILURE", "1");
        }
        secrets->saveSecret(proxy, nested);

        QCOMPARE(secrets->secret(proxy), nested);
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(finished.first().at(2).toBool(), nested.isEmpty());
        QVERIFY(finished.first().at(3).toBool());
        QCOMPARE(secrets->secret(proxy), nested);
        QCOMPARE(readSecret(SettingsStore().secrets(), proxy), nested);
        SettingsStore prefetched;
        prefetched.secrets()->prefetch();
        QTRY_COMPARE(prefetched.secrets()->cachedSecret(proxy), nested);
        comparePeerRead(settings, proxy, nested);
    }

    // Run by comparePeerRead only, as a separate process.
    void printWhatAPeerReads()
    {
        if (!qEnvironmentVariableIsSet(peerSecretVariable)) {
            QSKIP("Run by comparePeerRead");
        }
        if (qEnvironmentVariableIntValue(peerRestartsVariable) == 1) {
            migrateSavedSettings();
        }
        const auto secret = SecretStore::Secret(qEnvironmentVariableIntValue(peerSecretVariable));
        qInfo().noquote() << peerReadMarker + '[' + readSecret(SettingsStore().secrets(), secret).toUtf8() + ']';
    }

    // A save or clear asked for while another write of the same key waits on
    // the keyring answers for both, and its refusal is what saveFinished
    // reports. The key keeps what storage held; a refused clear
    // hides it until a deletion goes through.
    void aRefusedQueuedSaveIsReported_data()
    {
        QTest::addColumn<SecretStore::Secret>("secret");
        QTest::addColumn<QString>("nested");
        QTest::addColumn<QString>("kept");
        QTest::newRow("api-key") << SecretStore::Secret::OpenAiApiKey << QStringLiteral("last-key")
                                 << QStringLiteral("old-key");
        QTest::newRow("clear") << SecretStore::Secret::CliproxyApiKey << QString() << QString();
    }

    void aRefusedQueuedSaveIsReported()
    {
        QFETCH(SecretStore::Secret, secret);
        QFETCH(QString, nested);
        QFETCH(QString, kept);
        SettingsStore settings;
        settings.raw().clear();
        SecretStore *secrets = settings.secrets();
        const auto restore = qScopeGuard([secrets, secret] {
            qunsetenv("SPEECHER_TEST_KEYRING_WRITE_FAILURE");
            qunsetenv("SPEECHER_TEST_KEYRING_DELETE_FAILURE");
            savedSecret(secrets, secret, {});
        });
        // As QuickAccounts saves it: the OpenAI key only to the keyring.
        const auto save = [secrets, secret](const QString &value) {
            secret == SecretStore::Secret::OpenAiApiKey ? secrets->saveApiKey(value) : secrets->saveSecret(secret, value);
        };
        QVERIFY(savedSecret(secrets, secret, QStringLiteral("old-key")));
        qputenv("SPEECHER_TEST_KEYRING_WRITE_FAILURE", "1");
        qputenv("SPEECHER_TEST_KEYRING_DELETE_FAILURE", "1");
        // So that, without a keyring too, clearing the key needs one.
        settings.raw().setValue(SettingsKeys::SecretsInKeyring, QStringList{QStringLiteral("cliproxy-api-key")});
        QSignalSpy finished(secrets, &SecretStore::saveFinished);

        save(QStringLiteral("middle-key"));
        save(nested);

        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(finished.first().at(2).toBool(), nested.isEmpty());
        QVERIFY(!finished.first().at(3).toBool());
        QCOMPARE(secrets->secret(secret), kept);
        QCOMPARE(readSecret(SettingsStore().secrets(), secret), kept);
        comparePeerRead(settings, secret, kept);
    }

    // A save asked for while another waits on the keyring replaces the whole
    // request, so one that
    // may fall back to the settings file still does after a keyring-only save
    // of the same key that the keyring refused.
    void aQueuedSaveKeepsItsOwnFallback()
    {
        const auto openAi = SecretStore::Secret::OpenAiApiKey;
        SettingsStore settings;
        settings.raw().clear();
        SecretStore *secrets = settings.secrets();
        const auto restore = qScopeGuard([secrets, openAi] {
            qunsetenv("SPEECHER_TEST_KEYRING_WRITE_FAILURE");
            savedSecret(secrets, openAi, {});
        });
        QVERIFY(savedSecret(secrets, openAi, QStringLiteral("old-key")));
        qputenv("SPEECHER_TEST_KEYRING_WRITE_FAILURE", "1");

        QVERIFY(storedAfter(secrets, openAi, [&] {
            secrets->saveApiKey(QStringLiteral("new-key"));
            secrets->saveSecret(openAi, QStringLiteral("new-key"));
        }));

        QCOMPARE(secrets->secret(openAi), QStringLiteral("new-key"));
        QVERIFY(secrets->usesInsecureSettingsFallback());
        comparePeerRead(settings, openAi, QStringLiteral("new-key"));
    }

    // A save made while a settings save's keys wait on the keyring is newer
    // than the settings save's, even one of the value the key already has.
    void anOlderSettingsSaveLeavesALaterSaveAlone_data()
    {
        QTest::addColumn<QString>("kept");
        QTest::newRow("cached-key") << QStringLiteral("old-key");
        QTest::newRow("cached-empty-key") << QString();
    }

    void anOlderSettingsSaveLeavesALaterSaveAlone()
    {
        QFETCH(QString, kept);
        const auto refinement = SecretStore::Secret::RefinementEndpointKey;
        SettingsStore settings;
        settings.raw().clear();
        SecretStore *secrets = settings.secrets();
        const auto restore = qScopeGuard([secrets, refinement] {
            savedSecret(secrets, refinement, {});
            savedSecret(secrets, SecretStore::Secret::SpeechEndpointKey, {});
        });
        if (kept.isEmpty()) {
            QCOMPARE(secrets->secret(refinement), QString());
        } else {
            QVERIFY(savedSecret(secrets, refinement, kept));
        }
        AppSettings draft = settings.snapshot();
        draft.speech.endpoint.apiKey = QStringLiteral("speech-key");
        draft.refinement.endpoint.apiKey = QStringLiteral("refinement-key");

        QVERIFY(storedAfter(secrets, refinement, [&] {
            settings.applySnapshot(draft);
            secrets->saveSecret(refinement, kept);
        }));

        QCOMPARE(secrets->secret(refinement), kept);
        QCOMPARE(readSecret(SettingsStore().secrets(), refinement), kept);
        QTRY_COMPARE(readSecret(SettingsStore().secrets(), SecretStore::Secret::SpeechEndpointKey),
                     QStringLiteral("speech-key"));
    }

    // A read that waits on the keyring gets what the keyring held when it
    // asked. A save made meanwhile is newer: every read from then on returns
    // that, and the read's answer is dropped.
    void aSaveDuringAKeyringReadIsKept_data()
    {
        QTest::addColumn<QString>("nested");
        QTest::addColumn<bool>("writeRefused");
        QTest::newRow("replaced") << QStringLiteral("new-key") << false;
        QTest::newRow("replaced-in-the-settings-file") << QStringLiteral("new-key") << true;
        QTest::newRow("cleared") << QString() << false;
    }

    void aSaveDuringAKeyringReadIsKept()
    {
        QFETCH(QString, nested);
        QFETCH(bool, writeRefused);
        const auto proxy = SecretStore::Secret::CliproxyApiKey;
        {
            SettingsStore saved;
            saved.raw().clear();
            QVERIFY(savedSecret(saved.secrets(), proxy, QStringLiteral("old-key")));
            // Without a keyring the key went to the settings file, where a read
            // never waits. Listed as in the keyring, it waits for the refusal.
            if (!privateKeyring()) {
                saved.raw().remove(SecretStore::settingsKey(proxy));
                saved.raw().setValue(SettingsKeys::SecretsInKeyring, QStringList{QStringLiteral("cliproxy-api-key")});
            }
        }
        SettingsStore settings;
        SecretStore *secrets = settings.secrets();
        const auto restore = qScopeGuard([secrets, proxy] {
            qunsetenv("SPEECHER_TEST_KEYRING_WRITE_FAILURE");
            savedSecret(secrets, proxy, {});
        });
        QCOMPARE(secrets->secret(proxy), QString());
        QVERIFY(!secrets->isSecretKnown(proxy));
        if (writeRefused) {
            qputenv("SPEECHER_TEST_KEYRING_WRITE_FAILURE", "1");
        }
        QSignalSpy finished(secrets, &SecretStore::saveFinished);

        secrets->saveSecret(proxy, nested);

        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(secrets->cachedSecret(proxy), nested);
        QCOMPARE(readSecret(SettingsStore().secrets(), proxy), nested);
    }

    // A key a save says is stored is what another process reads, the moment
    // the save returns: with a keyring, the settings file's older copy is
    // gone; without one, the file holds the new key.
    void aStoredKeyIsWhatAnotherProcessReads()
    {
        const auto proxy = SecretStore::Secret::CliproxyApiKey;
        SettingsStore settings;
        settings.raw().clear();
        settings.raw().setValue(SecretStore::settingsKey(proxy), QStringLiteral("old-key"));
        settings.raw().sync();
        SecretStore *secrets = settings.secrets();
        const auto restore = qScopeGuard([secrets, proxy] { secrets->saveSecret(proxy, {}); });

        QVERIFY(savedSecret(secrets, proxy, QStringLiteral("new-key")));

        comparePeerRead(settings, proxy, QStringLiteral("new-key"));
    }

    // A clear the keyring refuses hides the key, a key only the keyring may
    // keep included, when the settings file still has an older copy of it.
    void aRefusedClearHidesTheSettingsFileCopy()
    {
        const auto openAi = SecretStore::Secret::OpenAiApiKey;
        SettingsStore settings;
        settings.raw().clear();
        settings.raw().setValue(SecretStore::settingsKey(openAi), QStringLiteral("old-key"));
        settings.raw().sync();
        SecretStore *secrets = settings.secrets();
        qputenv("SPEECHER_TEST_KEYRING_DELETE_FAILURE", "1");
        const auto restore = qScopeGuard([secrets, openAi] {
            qunsetenv("SPEECHER_TEST_KEYRING_DELETE_FAILURE");
            savedSecret(secrets, openAi, {});
        });

        QVERIFY(!savedApiKey(secrets, {}));

        QVERIFY(secrets->deletionPending(openAi));
        QCOMPARE(secrets->secret(openAi), QString());
        QCOMPARE(readSecret(SettingsStore().secrets(), openAi), QString());
        comparePeerRead(settings, openAi, QString());
    }

    // A refused save's reason is its own: a saveFinished handler that clears
    // another key, which needs no keyring, changes neither what lastError()
    // nor what saveFinished says.
    void aRefusedSaveKeepsItsReason()
    {
        const auto proxy = SecretStore::Secret::CliproxyApiKey;
        const auto speech = SecretStore::Secret::SpeechEndpointKey;
        SettingsStore settings;
        settings.raw().clear();
        SecretStore *secrets = settings.secrets();
        const auto restore = qScopeGuard([secrets, proxy, speech] {
            qunsetenv("SPEECHER_TEST_KEYRING_DELETE_FAILURE");
            savedSecret(secrets, proxy, {});
            savedSecret(secrets, speech, {});
        });
        QVERIFY(savedSecret(secrets, proxy, QStringLiteral("old-key")));
        // So that, without a keyring too, clearing the key needs one.
        settings.raw().setValue(SettingsKeys::SecretsInKeyring, QStringList{QStringLiteral("cliproxy-api-key")});
        qputenv("SPEECHER_TEST_KEYRING_DELETE_FAILURE", "1");
        QSignalSpy finished(secrets, &SecretStore::saveFinished);
        QObject handler;
        connect(secrets, &SecretStore::saveFinished, &handler, [secrets, proxy, speech](SecretStore::Secret secret) {
            if (secret == proxy) {
                secrets->saveSecret(speech, {});
            }
        });

        QVERIFY(!savedSecret(secrets, proxy, {}));

        const QString denied = QStringLiteral("Keyring deletion denied");
        QCOMPARE(secrets->lastError(), denied);
        QCOMPARE(finished.first().at(4).toString(), denied);
    }

    // A settings save that clears a key while another write of it waits on
    // the keyring is settled by that write, and its refusal is logged then.
    void aRefusedQueuedSettingsSaveIsLogged()
    {
        const auto proxy = SecretStore::Secret::CliproxyApiKey;
        SettingsStore settings;
        settings.raw().clear();
        SecretStore *secrets = settings.secrets();
        const auto restore = qScopeGuard([secrets, proxy] {
            qunsetenv("SPEECHER_TEST_KEYRING_DELETE_FAILURE");
            savedSecret(secrets, proxy, {});
        });
        QVERIFY(savedSecret(secrets, proxy, QStringLiteral("old-key")));
        AppSettings draft = settings.snapshot();
        draft.refinement.cliproxyApiKey.clear();
        // So that, without a keyring too, clearing the key needs one.
        settings.raw().setValue(SettingsKeys::SecretsInKeyring, QStringList{QStringLiteral("cliproxy-api-key")});
        QTest::ignoreMessage(QtWarningMsg, "Could not clear cliproxy-api-key: Keyring deletion denied");

        QVERIFY(!storedAfter(secrets, proxy, [&] {
            secrets->saveSecret(proxy, QStringLiteral("outer-key"));
            qputenv("SPEECHER_TEST_KEYRING_DELETE_FAILURE", "1");
            settings.applySnapshot(draft);
        }));
    }

    // Removing Speecher's secrets drops every save of them under way or
    // waiting its turn, and refuses one asked for while it runs, so what it
    // deleted stays deleted: for this store, a fresh one and another process.
    // Each refusal is logged, as every refused save is.
    void removingSecretsDropsTheirSaves()
    {
        // Read from the keyring by every store, so a key left there would show.
        const auto openAi = SecretStore::Secret::OpenAiApiKey;
        SettingsStore settings;
        settings.raw().clear();
        SecretStore *secrets = settings.secrets();
        const auto restore = qScopeGuard([secrets, openAi] { secrets->saveSecret(openAi, {}); });
        QSignalSpy finished(secrets, &SecretStore::saveFinished);
        for (int refusal = 0; refusal < 2; ++refusal) {
            QTest::ignoreMessage(QtWarningMsg, "Could not save openai-api-key: Speecher's secrets are being removed");
        }
        secrets->saveSecret(openAi, QStringLiteral("first-key"));
        secrets->saveSecret(openAi, QStringLiteral("queued-key"));

        std::optional<bool> removed;
        secrets->deleteKeyringSecrets([&removed](bool deleted, const QString &) { removed = deleted; });
        secrets->saveSecret(openAi, QStringLiteral("late-key"));

        // The removal settled the queued save, and the late one at once.
        QTRY_COMPARE(finished.count(), 2);
        QVERIFY(!finished.at(0).at(3).toBool());
        QVERIFY(!finished.at(1).at(3).toBool());
        QTRY_VERIFY(removed.has_value());
        QCOMPARE(finished.count(), 2);
        QCOMPARE(secrets->secret(openAi), QString());
        QCOMPARE(readSecret(SettingsStore().secrets(), openAi), QString());
        comparePeerRead(settings, openAi, QString());
    }

    // A removal asked for while another runs finishes with it, and saves
    // stay refused until then.
    void aSecondRemovalFinishesWithTheFirst()
    {
        const auto openAi = SecretStore::Secret::OpenAiApiKey;
        SettingsStore settings;
        settings.raw().clear();
        SecretStore *secrets = settings.secrets();
        int finished = 0;
        secrets->deleteKeyringSecrets([&finished](bool, const QString &) { ++finished; });
        secrets->deleteKeyringSecrets([&finished](bool, const QString &) { ++finished; });
        QTest::ignoreMessage(QtWarningMsg, "Could not save openai-api-key: Speecher's secrets are being removed");

        QVERIFY(!savedApiKey(secrets, QStringLiteral("late-key")));

        QTRY_COMPARE(finished, 2);
        QCOMPARE(secrets->secret(openAi), QString());
        comparePeerRead(settings, openAi, QString());
    }

#ifdef Q_OS_LINUX
    // Removal keeps the settings while the keyring keeps a key they hide, so
    // a clear the keyring refused stays hidden, for another process too. It
    // keeps nothing else: no other key the settings file holds, no other
    // setting.
    void removalKeepsOnlyWhatHidesAKey()
    {
        const auto openAi = SecretStore::Secret::OpenAiApiKey;
        const auto proxy = SecretStore::Secret::CliproxyApiKey;
        SettingsStore settings;
        settings.raw().clear();
        SecretStore *secrets = settings.secrets();
        const auto restore = qScopeGuard([secrets, openAi, proxy] {
            qunsetenv("SPEECHER_TEST_KEYRING_WRITE_FAILURE");
            qunsetenv("SPEECHER_TEST_KEYRING_DELETE_FAILURE");
            savedSecret(secrets, openAi, {});
            savedSecret(secrets, proxy, {});
        });
        QVERIFY(savedSecret(secrets, openAi, QStringLiteral("hidden-key")));
        qputenv("SPEECHER_TEST_KEYRING_DELETE_FAILURE", "1");
        QVERIFY(!savedSecret(secrets, openAi, {}));
        qputenv("SPEECHER_TEST_KEYRING_WRITE_FAILURE", "1");
        QVERIFY(savedSecret(secrets, proxy, QStringLiteral("plain-key")));
        settings.raw().setValue(SettingsKeys::VocabularyEntries, QStringLiteral("a term"));
        SpeecherRemoval removal(removalSteps(settings));
        QSignalSpy finished(&removal, &SpeecherRemoval::finished);

        removal.start(true);

        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(finished.first().first().value<RemovalReport>().steps.last().outcome,
                 RemovalStep::Outcome::KeptHidingKeys);
        QVERIFY(!settings.raw().contains(SettingsKeys::VocabularyEntries));
        QCOMPARE(readSecret(SettingsStore().secrets(), openAi), QString());
        comparePeerRead(settings, openAi, QString());
        comparePeerRead(settings, proxy, QString());
    }

    // Without a keyring to delete a key from, removal deletes the settings
    // and the keys they hold: they hide no key a keyring kept.
    void removalWithoutAKeyringDeletesTheSettings()
    {
        const auto proxy = SecretStore::Secret::CliproxyApiKey;
        SettingsStore settings;
        settings.raw().clear();
        SecretStore *secrets = settings.secrets();
        const auto restore = qScopeGuard([] {
            qunsetenv("SPEECHER_TEST_KEYRING_WRITE_FAILURE");
            qunsetenv("SPEECHER_TEST_KEYRING_DELETE_FAILURE");
        });
        const auto kept = keepSettingsHere(settings);
        // A private keyring refuses everything, as a missing one does.
        qputenv("SPEECHER_TEST_KEYRING_WRITE_FAILURE", "1");
        qputenv("SPEECHER_TEST_KEYRING_DELETE_FAILURE", "1");
        QVERIFY(savedSecret(secrets, proxy, QStringLiteral("plain-key")));
        SpeecherRemoval removal(removalSteps(settings));
        QSignalSpy finished(&removal, &SpeecherRemoval::finished);

        removal.start(true);

        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(finished.first().first().value<RemovalReport>().settingsDeleted());
        comparePeerRead(settings, proxy, QString());
    }

    // Removal deletes the settings an older Speecher left under its old app
    // id too, which the next start would import again, keys and all, and
    // their emptied folder, though QSettings can't write their file. When
    // they won't go, it says so, naming the current file too when that won't
    // keep only its markers, and keeps the next start from importing them:
    // either way, Speecher starting again reads none of their keys.
    void removalLeavesNoOlderSettingsToImport_data()
    {
        QTest::addColumn<bool>("olderReadOnly");
        QTest::addColumn<QByteArray>("malformedLine");
        QTest::addColumn<bool>("olderStay");
        QTest::addColumn<bool>("currentStays");
        QTest::addColumn<bool>("folderHidden");
        QTest::newRow("deleted") << false << QByteArray() << false << false << false;
        QTest::newRow("read-only") << true << QByteArray() << false << false << false;
        QTest::newRow("malformed") << false << QByteArrayLiteral("[unterminated\n") << false << false << false;
        QTest::newRow("won't go") << false << QByteArray() << true << false << false;
        QTest::newRow("won't go, nor the current") << false << QByteArray() << true << true << false;
#ifdef Q_OS_UNIX
        QTest::newRow("folder can't be searched") << false << QByteArray() << true << false << true;
#endif
    }

    void removalLeavesNoOlderSettingsToImport()
    {
        QFETCH(bool, olderReadOnly);
        QFETCH(QByteArray, malformedLine);
        QFETCH(bool, olderStay);
        QFETCH(bool, currentStays);
        QFETCH(bool, folderHidden);
        const auto proxy = SecretStore::Secret::CliproxyApiKey;
        SettingsStore settings;
        settings.raw().clear();
        settings.raw().setValue(SecretStore::settingsKey(proxy), QStringLiteral("key"));
        settings.raw().sync();
        const QString file = settings.raw().fileName();
        const auto kept = keepSettingsHere(settings);
        QString olderFile;
        {
            QSettings older(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("local.speecher"),
                            SettingsKeys::application());
            older.setValue(SecretStore::settingsKey(proxy), QStringLiteral("older-key"));
            older.sync();
            olderFile = older.fileName();
        }
        QFile appended(olderFile);
        QVERIFY(appended.open(QIODevice::Append));
        appended.write(malformedLine);
        appended.close();
        const QString olderFolder = QFileInfo(olderFile).absolutePath();
        const QFileDevice::Permissions permissions = QFile::permissions(olderFolder);
        const QFileDevice::Permissions filePermissions = QFile::permissions(file);
        const auto restore = qScopeGuard([&] {
            QFile::setPermissions(olderFolder, permissions);
            QFile::setPermissions(file, filePermissions);
            QFile::remove(olderFile);
            QDir().rmdir(olderFolder);
        });
        if (olderReadOnly) {
            QVERIFY(QFile::setPermissions(olderFile, QFileDevice::ReadOwner));
        }
        if (olderStay) {
            QVERIFY(QFile::setPermissions(olderFolder, QFileDevice::ReadOwner
                                                           | (folderHidden ? QFileDevice::WriteOwner
                                                                           : QFileDevice::ExeOwner)));
        }
        if (currentStays) {
            QVERIFY(QFile::setPermissions(file, QFileDevice::ReadOwner));
        }
        SpeecherRemoval removal(removalSteps(settings));
        QSignalSpy finished(&removal, &SpeecherRemoval::finished);

        removal.start(true);

        QTRY_COMPARE(finished.count(), 1);
        const RemovalStep step = finished.first().first().value<RemovalReport>().steps.last();
        QCOMPARE(step.outcome, olderStay ? RemovalStep::Outcome::Failed : RemovalStep::Outcome::Removed);
        const QString error = currentStays ? olderFile + QStringLiteral(" or the one at ") + file : olderFile;
        QCOMPARE(step.error, olderStay ? error : QString());
        QCOMPARE(QFileInfo::exists(olderFolder), olderStay);
        if (!currentStays) {
            comparePeerRead(settings, proxy, QString(), true);
        }
    }

    // An OpenAI key the keyring kept when removal couldn't delete it stays
    // hidden: every start reads that entry, so it would come back after a
    // reinstall.
    void removalHidesAnOpenAiKeyTheKeyringKept()
    {
        if (!privateKeyring()) {
            QSKIP("Requires the private keyring suite");
        }
        const auto openAi = SecretStore::Secret::OpenAiApiKey;
        SettingsStore settings;
        settings.raw().clear();
        SecretStore *secrets = settings.secrets();
        const auto restore = qScopeGuard([] {
            qunsetenv("SPEECHER_TEST_KEYRING_DELETE_FAILURE");
            removedSecrets(SettingsStore().secrets());
        });
        const auto kept = keepSettingsHere(settings);
        QVERIFY(savedApiKey(secrets, QStringLiteral("kept-key")));
        qputenv("SPEECHER_TEST_KEYRING_DELETE_FAILURE", "1");
        SpeecherRemoval removal(removalSteps(settings));
        QSignalSpy finished(&removal, &SpeecherRemoval::finished);

        removal.start(true);

        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(finished.first().first().value<RemovalReport>().steps.last().outcome,
                 RemovalStep::Outcome::KeptHidingKeys);
        comparePeerRead(settings, openAi, QString(), true);
    }
#endif

    // Settings deleted while a save waits on the keyring refuse it, though
    // nothing removed Speecher's secrets first.
    void aSaveTheSettingsDeletionInterruptsIsRefused()
    {
        SettingsStore settings;
        settings.raw().clear();
        SecretStore *secrets = settings.secrets();
        const auto restore = qScopeGuard([] { removedSecrets(SettingsStore().secrets()); });
        const auto kept = keepSettingsHere(settings);
        QTimer::singleShot(0, secrets, [&settings] { QVERIFY(settings.deleteAll(nullptr)); });

        QVERIFY(!savedApiKey(secrets, QStringLiteral("late-key")));

        QCOMPARE(secrets->lastError(), QStringLiteral("Speecher's settings were deleted"));
    }

    // A save that deleting the settings interrupts goes with them: a later
    // request, refused at once, is the only one to settle, so its number
    // never labels the older save's payload.
    void aSaveTheSettingsDeletionInterruptsGoesWithThem()
    {
        const auto proxy = SecretStore::Secret::CliproxyApiKey;
        SettingsStore settings;
        settings.raw().clear();
        SecretStore *secrets = settings.secrets();
        const auto restore = qScopeGuard([] { removedSecrets(SettingsStore().secrets()); });
        const auto kept = keepSettingsHere(settings);
        QSignalSpy finished(secrets, &SecretStore::saveFinished);
        secrets->saveSecret(proxy, QStringLiteral("old-key"));
        QVERIFY(settings.deleteAll(nullptr));

        QVERIFY(!savedSecret(secrets, proxy, {}));

        // Long enough for the keyring to answer the older save.
        QTest::qWait(200);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.first().at(1).toULongLong(), quint64(2));
        QVERIFY(finished.first().at(2).toBool());
    }

    // A save is judged by its own settings file, also after another store
    // deleted the settings, which moves where new readers look.
    void aSaveIsJudgedByItsOwnSettingsFile()
    {
        const auto proxy = SecretStore::Secret::CliproxyApiKey;
        SettingsStore settings;
        settings.raw().clear();
        SecretStore *secrets = settings.secrets();
        const auto restore = qScopeGuard([] { removedSecrets(SettingsStore().secrets()); });
        const auto kept = keepSettingsHere(settings);
        SettingsStore deleting;
        QVERIFY(deleting.deleteAll(nullptr));

        QVERIFY(savedSecret(secrets, proxy, QStringLiteral("new-key")));

        comparePeerRead(settings, proxy, QStringLiteral("new-key"));
    }

    // Once Speecher's settings are deleted, a save is refused before it
    // reaches the keyring or the settings file, so no key comes back.
    void aSaveAfterTheSettingsAreDeletedIsRefused()
    {
        const auto openAi = SecretStore::Secret::OpenAiApiKey;
        const auto proxy = SecretStore::Secret::CliproxyApiKey;
        SettingsStore settings;
        settings.raw().clear();
        SecretStore *secrets = settings.secrets();
        const auto kept = keepSettingsHere(settings);
        removedSecrets(secrets);
        QVERIFY(settings.deleteAll(nullptr));

        QVERIFY(!savedSecret(secrets, proxy, QStringLiteral("late-key")));
        QVERIFY(!savedApiKey(secrets, QStringLiteral("late-key")));

        QCOMPARE(secrets->secret(proxy), QString());
        comparePeerRead(settings, proxy, QString());
        comparePeerRead(settings, openAi, QString());
    }

#ifndef Q_OS_WIN
    // Readers made after a deletion keep their files in a private folder of
    // the process's own. A link another user left at the name the folder once
    // had, in the shared temporary folder, is neither written through nor
    // emptied.
    void readersAfterADeletionUseAPrivateFolderOfTheirOwn()
    {
        QTemporaryDir linked;
        QFile linkedFile(linked.filePath(QStringLiteral("kept")));
        QVERIFY(linkedFile.open(QIODevice::WriteOnly));
        linkedFile.close();
        const QString oldName =
            QDir::temp().filePath(QStringLiteral("speecher-removed-%1").arg(QCoreApplication::applicationPid()));
        QVERIFY(QFile::link(linked.path(), oldName));
        const auto unlink = qScopeGuard([oldName] { QFile::remove(oldName); });
        {
            SettingsStore settings;
            const auto kept = keepSettingsHere(settings);
            QVERIFY(settings.deleteAll(nullptr));

            QDir home = QFileInfo(QSettings(QSettings::defaultFormat(), QSettings::UserScope,
                                            QString::fromLatin1(SettingsKeys::Organization),
                                            SettingsKeys::application())
                                      .fileName())
                            .absoluteDir();
            QVERIFY(home.cdUp());
            QVERIFY(!QFileInfo(home.path()).isSymLink());
            QCOMPARE((QFile::permissions(home.path()) & openToOthers).toInt(), 0);
        }

        QVERIFY(QFile::exists(linkedFile.fileName()));
    }
#endif

    // A save is stored once the settings file saved what it changed, judged
    // by that save alone: not while the file won't save, however often the
    // save is retried, nor because QSettings found a malformed line when it
    // read the file. A save that changes nothing in the file needs no save.
    void aSaveIsStoredOnceTheSettingsFileSavesIt_data()
    {
        QTest::addColumn<QByteArray>("malformedLine");
        QTest::newRow("well-formed") << QByteArray();
        QTest::newRow("malformed") << QByteArrayLiteral("[unterminated\n");
    }

    void aSaveIsStoredOnceTheSettingsFileSavesIt()
    {
#ifdef Q_OS_WIN
        QSKIP("Windows keeps the settings in the registry, which file permissions don't lock");
#endif
        QFETCH(QByteArray, malformedLine);
        const auto proxy = SecretStore::Secret::CliproxyApiKey;
        QString file;
        {
            SettingsStore saved;
            saved.raw().clear();
            saved.raw().setValue(SecretStore::settingsKey(proxy), QStringLiteral("old-key"));
            saved.raw().sync();
            file = saved.raw().fileName();
        }
        QFile appended(file);
        QVERIFY(appended.open(QIODevice::Append));
        appended.write(malformedLine);
        appended.close();
        SettingsStore settings;
        SecretStore *secrets = settings.secrets();
        const QFileDevice::Permissions permissions = QFile::permissions(file);
        const auto restore = qScopeGuard([&] {
            QFile::setPermissions(file, permissions);
            savedSecret(secrets, proxy, {});
            savedSecret(secrets, SecretStore::Secret::OpenAiApiKey, {});
        });
        QVERIFY(QFile::setPermissions(file, QFileDevice::ReadOwner));

        QVERIFY(!savedSecret(secrets, proxy, QStringLiteral("new-key")));
        // The retry finds the key written and only the file left to save, so
        // it writes the key no second time.
        QTest::failOnWarning(QRegularExpression(QStringLiteral("keyring unavailable")));
        QVERIFY(!savedSecret(secrets, proxy, QStringLiteral("new-key")));
        comparePeerRead(settings, proxy, QStringLiteral("old-key"));
        // Clearing a key never saved changes nothing, nor does, with a
        // keyring, a key the settings file never keeps.
        QVERIFY(savedSecret(secrets, SecretStore::Secret::SpeechEndpointKey, {}));
        if (privateKeyring()) {
            QVERIFY(savedApiKey(secrets, QStringLiteral("api-key")));
        }

        QVERIFY(QFile::setPermissions(file, permissions));
        QVERIFY(savedSecret(secrets, proxy, QStringLiteral("new-key")));
        comparePeerRead(settings, proxy, QStringLiteral("new-key"));
    }

    // A save of the settings file stores every change made before it,
    // whoever saves it, so a key whose own save the file refused is stored
    // once another save went through, though the file won't save a later
    // change.
    void aSaveOfTheSettingsFileStoresEveryEarlierChange_data()
    {
        QTest::addColumn<bool>("byAnotherKey");
        QTest::newRow("another key's save") << true;
        QTest::newRow("the settings' own save") << false;
    }

    void aSaveOfTheSettingsFileStoresEveryEarlierChange()
    {
#ifdef Q_OS_WIN
        QSKIP("Windows keeps the settings in the registry, which file permissions don't lock");
#endif
        QFETCH(bool, byAnotherKey);
        const auto proxy = SecretStore::Secret::CliproxyApiKey;
        const auto speech = SecretStore::Secret::SpeechEndpointKey;
        SettingsStore settings;
        settings.raw().clear();
        settings.raw().setValue(SettingsKeys::VocabularyEntries, QStringLiteral("a term"));
        settings.raw().sync();
        SecretStore *secrets = settings.secrets();
        const QString file = settings.raw().fileName();
        const QFileDevice::Permissions permissions = QFile::permissions(file);
        const auto restore = qScopeGuard([&] {
            QFile::setPermissions(file, permissions);
            savedSecret(secrets, proxy, {});
            savedSecret(secrets, speech, {});
        });
        QVERIFY(QFile::setPermissions(file, QFileDevice::ReadOwner));
        QVERIFY(!savedSecret(secrets, proxy, QStringLiteral("proxy-key")));
        QVERIFY(QFile::setPermissions(file, permissions));
        if (byAnotherKey) {
            QVERIFY(savedSecret(secrets, speech, QStringLiteral("speech-key")));
        } else {
            settings.raw().sync();
        }
        QVERIFY(QFile::setPermissions(file, QFileDevice::ReadOwner));
        settings.raw().setValue(SettingsKeys::VocabularyEntries, QStringLiteral("another term"));

        QVERIFY(savedSecret(secrets, proxy, QStringLiteral("proxy-key")));

        comparePeerRead(settings, proxy, QStringLiteral("proxy-key"));
    }

    void unreadDraftSurvivesPrefetchCompletion()
    {
        if (qEnvironmentVariableIntValue("SPEECHER_TEST_PRIVATE_KEYRING") != 1)
            QSKIP("Requires the private keyring suite");
        SettingsStore saved;
        saved.raw().clear();
        QVERIFY(savedSecret(saved.secrets(), SecretStore::Secret::CliproxyApiKey, QStringLiteral("keep-me")));
        SettingsStore settings;
        qputenv("SPEECHER_TEST_KEYRING_READ_TIMEOUT", "1");
        const AppSettings draft = settings.snapshot();
        QTRY_VERIFY(!settings.secrets()->readFailure(SecretStore::Secret::CliproxyApiKey).isEmpty());
        qunsetenv("SPEECHER_TEST_KEYRING_READ_TIMEOUT");
        QVERIFY(draft.refinement.cliproxyApiKey.isEmpty());
        settings.secrets()->prefetch();
        QTRY_COMPARE(settings.secrets()->cachedSecret(SecretStore::Secret::CliproxyApiKey),
                     QStringLiteral("keep-me"));
        settings.applySnapshot(draft);
        QCOMPARE(readSecret(SettingsStore().secrets(), SecretStore::Secret::CliproxyApiKey), QStringLiteral("keep-me"));
        QVERIFY(savedSecret(saved.secrets(), SecretStore::Secret::CliproxyApiKey, {}));
    }

    void failedDeletionStaysClearedAfterRestart()
    {
        if (qEnvironmentVariableIntValue("SPEECHER_TEST_PRIVATE_KEYRING") != 1)
            QSKIP("Requires the private keyring suite");
        SettingsStore settings;
        settings.raw().clear();
        QVERIFY(savedSecret(settings.secrets(), SecretStore::Secret::CliproxyApiKey, QStringLiteral("remove-me")));
        qputenv("SPEECHER_TEST_KEYRING_DELETE_FAILURE", "1");
        const auto restore = qScopeGuard([] { qunsetenv("SPEECHER_TEST_KEYRING_DELETE_FAILURE"); });
        QVERIFY(!savedSecret(settings.secrets(), SecretStore::Secret::CliproxyApiKey, {}));
        QVERIFY(settings.secrets()->lastError().contains(QStringLiteral("denied")));
        SettingsStore reopened;
        reopened.secrets()->prefetch();
        QCOMPARE(readSecret(reopened.secrets(), SecretStore::Secret::CliproxyApiKey), QString());
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
        settings.raw().setValue(SettingsKeys::SecretsInKeyring,
            QStringList{QStringLiteral("cliproxy-api-key"), QStringLiteral("speech-endpoint-key")});
        settings.edit([&](AppSettings &draft) {
            draft.refinement.providerId = refiner == QStringLiteral("endpoint-inherited") ? QStringLiteral("endpoint") : refiner;
            draft.speech.providerId = speechProvider;
            draft.accounts.openAiAuthMode = OpenAiAuthMode::Cliproxy;
            draft.accounts.anthropicAuthMode = AnthropicAuthMode::Cliproxy;
            draft.refinement.cliproxyBaseUrl = remote ? QStringLiteral("http://proxy.example:8317") : QString();
            return true;
        });
        settings.raw().setValue(SettingsKeys::RefinementEndpointPreset,
                                refiner == QStringLiteral("endpoint-inherited") ? QString() : QStringLiteral("cliproxy"));
        // Inherited: a Custom endpoint without a key of its own, on the proxy's server.
        settings.raw().setValue(SettingsKeys::RefinementEndpointBaseUrl, QStringLiteral("http://proxy.example:8317/v1"));
        settings.raw().setValue(SettingsKeys::SpeechEndpointBaseUrl, QStringLiteral("http://speech.example"));
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
        if (qEnvironmentVariableIntValue("SPEECHER_TEST_PRIVATE_KEYRING") != 1)
            QSKIP("Requires the private keyring suite");
        FakeServer server;
        server.route("POST /v1/audio/transcriptions",
                     httpResponse("200 OK", "application/json", "{\"text\":\"Hello\"}"));
        {
            SettingsStore saved;
            saved.raw().clear();
            saved.edit([](AppSettings &draft) {
                draft.speech.providerId = QStringLiteral("endpoint");
                draft.refinement.providerId = QStringLiteral("none");
                return true;
            });
            saved.raw().setValue(SettingsKeys::SpeechEndpointBaseUrl, server.origin());
            QVERIFY(savedSecret(saved.secrets(), SecretStore::Secret::SpeechEndpointKey, QStringLiteral("speech-key")));
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

    // The settings window's snapshot reads a saved key for an endpoint that
    // is not selected; dictation start does not read that key; and when
    // the keyring does not answer, switching to the endpoint and saving the
    // empty field keeps the stored key.
    void onlyTheSettingsSnapshotReadsUnselectedKeys()
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
        QVERIFY(settings.dictationSnapshot().speech.providerId != QStringLiteral("endpoint"));
        settings.dictationSnapshot();
        QVERIFY2(settings.secrets()->lastError().isEmpty(), qPrintable(settings.secrets()->lastError()));

        AppSettings draft = settings.snapshot();
        QTRY_VERIFY(settings.secrets()->lastError().contains(QStringLiteral("timed out")));
        QCOMPARE(draft.speech.endpoint.apiKey, QString());

        draft.speech.providerId = QStringLiteral("endpoint");
        draft.speech.endpoint.baseUrl = QStringLiteral("http://whisper.local:8080");
        settings.applySnapshot(draft);
        QCOMPARE(settings.dictationSnapshot().speech.providerId, QStringLiteral("endpoint"));
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
        QCOMPARE(settings.secrets()->secret(SecretStore::Secret::CliproxyApiKey), QStringLiteral("legacy-proxy-key"));
        QCOMPARE(settings.snapshot().refinement.cliproxyApiKey, QStringLiteral("legacy-proxy-key"));
        const auto inKeyring = [&settings] {
            return settings.raw().value(SettingsKeys::SecretsInKeyring).toStringList()
                .contains(QStringLiteral("cliproxy-api-key"));
        };
        // The migration's write answers through the event loop.
        QTRY_VERIFY(!privateKeyring() || inKeyring());
        const bool migrated = inKeyring();
        QCOMPARE(settings.raw().contains(SettingsKeys::CliproxyApiKey), !migrated);
        if (qEnvironmentVariableIntValue("SPEECHER_TEST_PRIVATE_KEYRING") == 1) {
            QVERIFY(migrated);
        }
        qInfo() << "cliproxy key migrated to the keyring:" << migrated;

        QVERIFY(savedSecret(settings.secrets(), SecretStore::Secret::CliproxyApiKey, {}));
        QCOMPARE(readSecret(SettingsStore().secrets(), SecretStore::Secret::CliproxyApiKey), QString());
    }
};

int runCustomEndpointsTests(int argc, char **argv)
{
    CustomEndpointsTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_custom_endpoints.moc"
