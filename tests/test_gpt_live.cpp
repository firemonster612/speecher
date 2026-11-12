#include "app/CommandLine.h"
#include "app/LiveCommand.h"
#include "common/test_suites.h"
#include "core/SettingsStore.h"
#include "providers/GptLiveProtocol.h"
#include "providers/GptLiveSession.h"
#include "providers/OpenAiAuthProvider.h"
#ifdef SPEECHER_WITH_GPT_LIVE
#include "providers/GptLiveCall.h"
#endif

#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QtEndian>

#include <cmath>
#include <numbers>
#include <sstream>

using namespace speecher;

namespace {

// s16le mono samples of a tone.
QByteArray tone(double hz, int sampleRate, int samples, double amplitude = 10000)
{
    QByteArray pcm(samples * 2, Qt::Uninitialized);
    for (int index = 0; index < samples; ++index) {
        qToLittleEndian(qint16(std::lround(amplitude * std::sin(2 * std::numbers::pi * hz * index / sampleRate))),
                        pcm.data() + 2 * index);
    }
    return pcm;
}

double rms(const QByteArray &pcm, qsizetype skipSamples = 0)
{
    double sum = 0;
    const qsizetype samples = pcm.size() / 2;
    for (qsizetype index = skipSamples; index < samples; ++index) {
        const double sample = qFromLittleEndian<qint16>(pcm.constData() + 2 * index);
        sum += sample * sample;
    }
    return std::sqrt(sum / double(samples - skipSamples));
}

void writeWav(const QString &path, const QByteArray &pcm, int sampleRate)
{
    QByteArray header;
    const auto u32 = [&header](quint32 value) {
        char bytes[4];
        qToLittleEndian(value, bytes);
        header.append(bytes, 4);
    };
    const auto u16 = [&header](quint16 value) {
        char bytes[2];
        qToLittleEndian(value, bytes);
        header.append(bytes, 2);
    };
    header.append("RIFF");
    u32(36 + quint32(pcm.size()));
    header.append("WAVEfmt ");
    u32(16);
    u16(1);
    u16(1);
    u32(quint32(sampleRate));
    u32(quint32(sampleRate) * 2);
    u16(2);
    u16(16);
    header.append("data");
    u32(quint32(pcm.size()));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(header + pcm);
}

GptLiveEvent made(GptLiveEvent::Kind kind, const QString &text = {})
{
    GptLiveEvent event{kind, text};
    return event;
}

// A session that answers as a script says: it starts, hears the audio sent,
// and once it has heard answerAfterMs, plays the user's and the assistant's
// turns, eventGapMs apart. close() says goodbye as the real one does, unless
// it hangs up without one.
class FakeSession final : public GptLiveSession {
public:
    void start(const QString &accessToken, const QString &, const GptLiveSessionOptions &options) override
    {
        token = accessToken;
        startedWith = options;
        QTimer::singleShot(0, this, [this] {
            if (!failStart.isEmpty()) {
                emit failed(failStart);
                emit closed();
                return;
            }
            emit event(made(GptLiveEvent::Kind::SessionStarted));
        });
    }
    void sendAudio(const QByteArray &pcm16k) override
    {
        heard += pcm16k;
        if (!answered && heard.size() >= answerAfterMs * 32) {
            answered = true;
            for (qsizetype index = 0; index < script.size(); ++index) {
                QTimer::singleShot(int(index * eventGapMs), this, [this, scripted = script.at(index)] {
                    emit event(scripted);
                    // The server ending the session closes the sideband.
                    if (scripted.kind == GptLiveEvent::Kind::SessionClosed) {
                        emit closed();
                    }
                });
            }
        }
    }
    qint64 queuedMs() const override { return 0; }
    void close() override
    {
        ++closeCalls;
        QTimer::singleShot(0, this, [this] {
            if (sayGoodbye) {
                emit event(made(GptLiveEvent::Kind::SessionClosed, QStringLiteral("client_request")));
            }
            emit closed();
        });
    }

    QList<GptLiveEvent> script;
    qint64 answerAfterMs = 100;
    int eventGapMs = 0;
    bool sayGoodbye = true;
    QString failStart;
    QString token;
    GptLiveSessionOptions startedWith;
    QByteArray heard;
    bool answered = false;
    int closeCalls = 0;
};

QList<GptLiveEvent> helloScript(const QByteArray &assistantPcm)
{
    GptLiveEvent userWord = made(GptLiveEvent::Kind::UserTranscriptDelta, QStringLiteral("Hello"));
    userWord.startMs = 200;
    userWord.endMs = 400;
    GptLiveEvent audio = made(GptLiveEvent::Kind::AssistantAudio);
    audio.pcm = assistantPcm;
    return {userWord,
            made(GptLiveEvent::Kind::UserTurn, QStringLiteral("Hello there")),
            made(GptLiveEvent::Kind::AssistantTranscriptDelta, QStringLiteral(" Hi")),
            audio,
            made(GptLiveEvent::Kind::AssistantTurn, QStringLiteral("Hi, how can I help?"))};
}

QStringList jsonTypes(const std::string &out)
{
    QStringList types;
    for (const QString &line : QString::fromStdString(out).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        types << QJsonDocument::fromJson(line.toUtf8()).object().value(QStringLiteral("type")).toString();
    }
    return types;
}

CommandLineDecision parseLive(const QStringList &arguments)
{
    return parseCommandLine(QStringList{QStringLiteral("speecher"), QStringLiteral("live")} + arguments, {});
}

} // namespace

class GptLiveTests : public QObject {
    Q_OBJECT

private slots:
    void callRequestCarriesTheChatGptLogin()
    {
        const QNetworkRequest request = gptLiveCallRequest(QStringLiteral("token"), QStringLiteral("account"));
        QCOMPARE(request.url().toString(),
                 QStringLiteral("https://chatgpt.com/backend-api/codex/realtime/calls?intent=quicksilver&architecture=avas"));
        QCOMPARE(request.rawHeader("Authorization"), QByteArray("Bearer token"));
        QCOMPARE(request.rawHeader("openai-alpha"), QByteArray("quicksilver=v2"));
        QCOMPARE(request.rawHeader("ChatGPT-Account-ID"), QByteArray("account"));
        QCOMPARE(request.header(QNetworkRequest::ContentTypeHeader).toString(), QStringLiteral("application/json"));
        QVERIFY(!gptLiveCallRequest(QStringLiteral("token"), {}).hasRawHeader("ChatGPT-Account-ID"));

        const QJsonObject body = QJsonDocument::fromJson(gptLiveCallBody(
            QStringLiteral("v=0\r\n"), {QStringLiteral("Be brief."), QStringLiteral("cove")})).object();
        QCOMPARE(body.value(QStringLiteral("sdp")).toString(), QStringLiteral("v=0\r\n"));
        QCOMPARE(body.value(QStringLiteral("session")).toObject(),
                 QJsonObject({{QStringLiteral("instructions"), QStringLiteral("Be brief.")},
                              {QStringLiteral("audio"),
                               QJsonObject{{QStringLiteral("output"),
                                            QJsonObject{{QStringLiteral("voice"), QStringLiteral("cove")}}}}}}));
        // A session with no instructions takes the server's own.
        QVERIFY(!QJsonDocument::fromJson(gptLiveCallBody({}, {}))
                     .object()
                     .value(QStringLiteral("session"))
                     .toObject()
                     .contains(QStringLiteral("instructions")));

        QCOMPARE(gptLiveCallId("/v1/realtime/calls/rtc_u24_abc"), QStringLiteral("rtc_u24_abc"));
        QCOMPARE(gptLiveCallId("/v1/realtime/calls/rtc_u24_abc?x=1"), QStringLiteral("rtc_u24_abc"));
        QCOMPARE(gptLiveCallId("/v1/realtime/calls/"), QString());
        QCOMPARE(gptLiveCallId(""), QString());

        const QNetworkRequest sideband =
            gptLiveSidebandRequest(QStringLiteral("rtc_u24_abc"), QStringLiteral("token"), {});
        QCOMPARE(sideband.url().toString(), QStringLiteral("wss://api.openai.com/v1/live/rtc_u24_abc"));
        QCOMPARE(sideband.rawHeader("Authorization"), QByteArray("Bearer token"));
        QCOMPARE(sideband.rawHeader("openai-alpha"), QByteArray("quicksilver=v2"));
    }

    // The shapes the sideband sent on 2026-11-12 (docs/research/0010).
    void sidebandEventsBecomeWhatAClientNeeds()
    {
        using Kind = GptLiveEvent::Kind;
        QCOMPARE(parseGptLiveEvent(R"({"type":"session.started","session":{"id":"rtc_x","status":"active"}})").kind,
                 Kind::SessionStarted);

        GptLiveEvent event = parseGptLiveEvent(
            R"({"type":"input_transcript.added","end_ms":1000,"item":{"id":"item_1","type":"input_transcript","text":"And"},"start_ms":800})");
        QCOMPARE(event.kind, Kind::UserTranscriptDelta);
        QCOMPARE(event.text, QStringLiteral("And"));
        QCOMPARE(event.startMs, 800);
        QCOMPARE(event.endMs, 1000);

        event = parseGptLiveEvent(
            R"({"type":"output_transcript.added","end_ms":11400,"item":{"type":"output_transcript","text":" Yep"},"start_ms":11200})");
        QCOMPARE(event.kind, Kind::AssistantTranscriptDelta);
        QCOMPARE(event.text, QStringLiteral(" Yep"));

        event = parseGptLiveEvent(
            R"({"type":"turn.done","turn":{"id":"turn_1","end_ms":11400,"role":"user","start_ms":800,"transcript":" And so, my fellow Americans"}})");
        QCOMPARE(event.kind, Kind::UserTurn);
        QCOMPARE(event.text, QStringLiteral("And so, my fellow Americans"));
        QCOMPARE(event.startMs, 800);
        QCOMPARE(parseGptLiveEvent(R"({"type":"turn.done","turn":{"role":"assistant","transcript":"Yep."}})").kind,
                 Kind::AssistantTurn);
        QCOMPARE(parseGptLiveEvent(R"({"type":"turn.done","turn":{"role":"system","transcript":"x"}})").kind,
                 Kind::Ignored);

        event = parseGptLiveEvent(R"({"type":"session.output_audio.delta","delta":"AQACAA=="})");
        QCOMPARE(event.kind, Kind::AssistantAudio);
        QCOMPARE(event.pcm, QByteArray("\x01\x00\x02\x00", 4));

        event = parseGptLiveEvent(
            R"({"type":"session.usage.updated","usage":{"audio_duration_ms":4200},"usage_limit":{"status":null}})");
        QCOMPARE(event.kind, Kind::Usage);
        QCOMPARE(event.audioMs, 4200);
        event = parseGptLiveEvent(
            R"({"type":"session.closed","reason":"client_request","usage":{"audio_duration_ms":0}})");
        QCOMPARE(event.kind, Kind::SessionClosed);
        QCOMPARE(event.text, QStringLiteral("client_request"));

        event = parseGptLiveEvent(
            R"({"type":"error","error":{"type":"invalid_request_error","message":"Voice cannot be updated after initialization."}})");
        QCOMPARE(event.kind, Kind::Error);
        QCOMPARE(event.text, QStringLiteral("Voice cannot be updated after initialization."));

        // The input's echo, turn bookkeeping and anything new are not for the client.
        for (const char *ignored : {R"({"type":"session.input_audio.append","audio":"AAAA"})",
                                    R"({"type":"turn.created","turn":{"role":"user"}})",
                                    R"({"type":"turn.delta","delta":" so"})", "not json", "{}"}) {
            QCOMPARE(parseGptLiveEvent(QByteArrayView(ignored)).kind, Kind::Ignored);
        }
    }

    void pcmuIsG711MuLaw()
    {
        const auto encoded = [](qint16 sample) {
            char bytes[2];
            qToLittleEndian(sample, bytes);
            return quint8(pcmuFromPcm16(QByteArrayView(bytes, 2)).at(0));
        };
        QCOMPARE(encoded(0), quint8(0xFF));
        QCOMPARE(encoded(-1), quint8(0x7F));
        QCOMPARE(encoded(1000), quint8(0xCE));
        QCOMPARE(encoded(32767), quint8(0x80));
        QCOMPARE(encoded(-32768), quint8(0x00));
        QCOMPARE(pcmuFromPcm16(tone(440, 8000, 160)).size(), 160);
    }

    void downsamplingKeepsSpeechAndDropsWhatWouldFoldBack()
    {
        const QByteArray low = tone(1000, 16000, 3200);
        Downsampler16kTo8k downsampler;
        const QByteArray lowOut = downsampler.process(low);
        QCOMPARE(lowOut.size(), low.size() / 2);
        // Past the filter's start, a 1 kHz tone comes through whole.
        QVERIFY2(rms(lowOut, 32) > 0.95 * rms(low), qPrintable(QString::number(rms(lowOut, 32))));

        Downsampler16kTo8k high;
        QVERIFY(rms(high.process(tone(6000, 16000, 3200)), 32) < 0.05 * rms(tone(6000, 16000, 3200)));

        // Cut anywhere, a stream comes out as it would whole.
        Downsampler16kTo8k chunked;
        QByteArray pieces;
        for (qsizetype at = 0; at < low.size(); at += 322) {
            pieces += chunked.process(QByteArrayView(low).sliced(at, std::min<qsizetype>(322, low.size() - at)));
        }
        QCOMPARE(pieces, lowOut);
    }

    void eventsPrintAsJsonForScripts()
    {
        using Kind = GptLiveEvent::Kind;
        GptLiveEvent delta = made(Kind::UserTranscriptDelta, QStringLiteral("Hi"));
        delta.startMs = 10;
        delta.endMs = 20;
        QCOMPARE(liveEventJson(delta, true),
                 std::optional(QJsonObject({{QStringLiteral("type"), QStringLiteral("user.transcript.delta")},
                                            {QStringLiteral("text"), QStringLiteral("Hi")},
                                            {QStringLiteral("start_ms"), 10},
                                            {QStringLiteral("end_ms"), 20}})));
        QCOMPARE(liveEventJson(made(Kind::AssistantTurn, QStringLiteral("Yes.")), true)->value(QStringLiteral("type")),
                 QStringLiteral("assistant.turn"));
        GptLiveEvent audio = made(Kind::AssistantAudio);
        audio.pcm = QByteArray("\x01\x00", 2);
        QCOMPARE(liveEventJson(audio, true)->value(QStringLiteral("pcm_s16le_24k")).toString(), QStringLiteral("AQA="));
        QVERIFY(!liveEventJson(audio, false));
        GptLiveEvent usage = made(Kind::Usage);
        usage.audioMs = 4200;
        QCOMPARE(liveEventJson(usage, true)->value(QStringLiteral("audio_ms")).toInt(), 4200);
        QCOMPARE(liveEventJson(made(Kind::Error, QStringLiteral("no")), true)->value(QStringLiteral("message")),
                 QStringLiteral("no"));
        QVERIFY(!liveEventJson(made(Kind::Ignored), true));
    }

    void commandLineTakesAFileOrStdin()
    {
        QTemporaryDir dir;
        const QString audio = dir.filePath(QStringLiteral("call.wav"));
        writeWav(audio, tone(440, 16000, 1600), 16000);

        CommandLineDecision decision = parseLive({QStringLiteral("--voice"), QStringLiteral("Cove"),
                                                  QStringLiteral("--instructions"), QStringLiteral("Be brief."),
                                                  QStringLiteral("--audio-out"), dir.filePath(QStringLiteral("out.wav")),
                                                  QStringLiteral("--linger"), QStringLiteral("5"), audio});
        QCOMPARE(decision.mode, LaunchMode::LiveHeadless);
        QCOMPARE(decision.live.input, audio);
        QCOMPARE(decision.live.session.voice, QStringLiteral("cove"));
        QCOMPARE(decision.live.session.instructions, QStringLiteral("Be brief."));
        QCOMPARE(decision.live.lingerMs, 5000);
        QVERIFY(!decision.live.json);

        decision = parseLive({QStringLiteral("--json"), QStringLiteral("--rate"), QStringLiteral("8000"),
                              QStringLiteral("-")});
        QCOMPARE(decision.mode, LaunchMode::LiveHeadless);
        QCOMPARE(decision.live.input, QStringLiteral("-"));
        QCOMPARE(decision.live.stdinRate, 8000);
        QVERIFY(decision.live.json);
        QCOMPARE(decision.live.session.voice, kDefaultGptLiveVoice);

        const QString instructions = dir.filePath(QStringLiteral("instructions.txt"));
        QFile file(instructions);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("You answer the phone.\n");
        file.close();
        QCOMPARE(parseLive({QStringLiteral("--instructions-file"), instructions, QStringLiteral("-")})
                     .live.session.instructions,
                 QStringLiteral("You answer the phone."));

        for (const QStringList &mistake : {QStringList{},
                                           QStringList{audio, audio},
                                           QStringList{QStringLiteral("--voice"), QStringLiteral("marin"), audio},
                                           QStringList{QStringLiteral("--rate"), QStringLiteral("8000"), audio},
                                           QStringList{QStringLiteral("--rate"), QStringLiteral("4000"), QStringLiteral("-")},
                                           QStringList{QStringLiteral("--json"), QStringLiteral("--audio-out"),
                                                       QStringLiteral("-"), QStringLiteral("-")},
                                           QStringList{QStringLiteral("--audio-out"),
                                                       dir.filePath(QStringLiteral("missing/out.wav")), audio},
                                           QStringList{QStringLiteral("--linger"), QStringLiteral("0"), audio},
                                           QStringList{QStringLiteral("--instructions-file"),
                                                       dir.filePath(QStringLiteral("missing.txt")), audio},
                                           QStringList{dir.filePath(QStringLiteral("missing.wav"))},
                                           QStringList{QStringLiteral("--frobnicate"), audio}}) {
            const CommandLineDecision refused = parseLive(mistake);
            QVERIFY2(refused.mode == LaunchMode::Exit && refused.exitCode == 2, qPrintable(mistake.join(QLatin1Char(' '))));
        }
    }

    // A file's audio all reaches the session, every event prints as a JSON
    // line, and the session closes once the assistant has answered.
    void fileSessionPrintsJsonEvents()
    {
        QTemporaryDir dir;
        const QString audio = dir.filePath(QStringLiteral("call.wav"));
        writeWav(audio, tone(440, 16000, 16000), 16000);
        FakeSession session;
        session.script = helloScript(QByteArray(4800, '\x01'));
        LiveOptions options;
        options.input = audio;
        options.json = true;
        options.session.instructions = QStringLiteral("Be brief.");
        std::istringstream in;
        std::ostringstream out;
        std::ostringstream err;

        QCOMPARE(runLiveSession(options, session, QStringLiteral("token"), {}, in, out, err), 0);
        QCOMPARE(session.token, QStringLiteral("token"));
        QCOMPARE(session.startedWith.instructions, QStringLiteral("Be brief."));
        QCOMPARE(session.closeCalls, 1);
        // All of the second of audio, as 16 kHz mono.
        QVERIFY2(std::abs(session.heard.size() - 32000) <= 640, qPrintable(QString::number(session.heard.size())));
        QCOMPARE(jsonTypes(out.str()),
                 (QStringList{QStringLiteral("session.started"), QStringLiteral("user.transcript.delta"),
                              QStringLiteral("user.turn"), QStringLiteral("assistant.transcript.delta"),
                              QStringLiteral("assistant.audio"), QStringLiteral("assistant.turn"),
                              QStringLiteral("session.closed")}));
    }

    // Raw PCM on stdin at its own rate, the turns as lines, and the
    // assistant's audio in a WAV file.
    void stdinSessionWritesTurnsAndAWav()
    {
        QTemporaryDir dir;
        FakeSession session;
        const QByteArray assistantPcm(9600, '\x02');
        session.script = helloScript(assistantPcm);
        LiveOptions options;
        options.input = QStringLiteral("-");
        options.stdinRate = 8000;
        options.audioOut = dir.filePath(QStringLiteral("assistant.wav"));
        // Half a second at 8 kHz.
        std::istringstream in(tone(300, 8000, 4000).toStdString());
        std::ostringstream out;
        std::ostringstream err;

        QCOMPARE(runLiveSession(options, session, QStringLiteral("token"), {}, in, out, err), 0);
        QCOMPARE(QString::fromStdString(out.str()), QStringLiteral("user: Hello there\nassistant: Hi, how can I help?\n"));
        QVERIFY2(std::abs(session.heard.size() - 16000) <= 640, qPrintable(QString::number(session.heard.size())));
        QFile wav(options.audioOut);
        QVERIFY(wav.open(QIODevice::ReadOnly));
        const QByteArray written = wav.readAll();
        QCOMPARE(written.size(), 44 + assistantPcm.size());
        QCOMPARE(written.mid(0, 4), QByteArray("RIFF"));
        QCOMPARE(qFromLittleEndian<quint32>(written.constData() + 24), quint32(24000));
        QCOMPARE(qFromLittleEndian<quint32>(written.constData() + 40), quint32(assistantPcm.size()));
        QCOMPARE(written.mid(44), assistantPcm);
    }

    // With the audio on stdout, the turns go to stderr.
    void audioOnStdoutMovesTheTurnsToStderr()
    {
        FakeSession session;
        const QByteArray assistantPcm(480, '\x03');
        session.script = helloScript(assistantPcm);
        LiveOptions options;
        options.input = QStringLiteral("-");
        options.audioOut = QStringLiteral("-");
        std::istringstream in(tone(300, 24000, 4800).toStdString());
        std::ostringstream out;
        std::ostringstream err;

        QCOMPARE(runLiveSession(options, session, QStringLiteral("token"), {}, in, out, err), 0);
        QCOMPARE(QByteArray::fromStdString(out.str()), assistantPcm);
        QVERIFY(QString::fromStdString(err.str()).contains(QStringLiteral("assistant: Hi, how can I help?")));
    }

    void sessionFailuresExitWithOne()
    {
        std::ostringstream out;
        std::ostringstream err;
        LiveOptions options;
        options.input = QStringLiteral("-");
        std::istringstream in;

        FakeSession refused;
        refused.failStart = QStringLiteral("GPT-Live refused the call: 403");
        QCOMPARE(runLiveSession(options, refused, QStringLiteral("token"), {}, in, out, err), 1);
        QVERIFY(QString::fromStdString(err.str()).contains(QStringLiteral("GPT-Live refused the call: 403")));

        // The server ending the session itself is a failure.
        FakeSession ended;
        ended.script = {made(GptLiveEvent::Kind::SessionClosed, QStringLiteral("usage_limit"))};
        ended.answerAfterMs = 0;
        std::istringstream speech(tone(300, 24000, 2400).toStdString());
        err.str({});
        QCOMPARE(runLiveSession(options, ended, QStringLiteral("token"), {}, speech, out, err), 1);
        QVERIFY(QString::fromStdString(err.str()).contains(QStringLiteral("GPT-Live ended the session: usage_limit")));
    }

    // Input that gets no answer still ends, once the linger is up.
    void unansweredInputClosesAfterTheLinger()
    {
        FakeSession session;
        LiveOptions options;
        options.input = QStringLiteral("-");
        options.lingerMs = 300;
        std::istringstream in(tone(300, 24000, 2400).toStdString());
        std::ostringstream out;
        std::ostringstream err;
        QElapsedTimer clock;
        clock.start();

        QCOMPARE(runLiveSession(options, session, QStringLiteral("token"), {}, in, out, err), 0);
        QCOMPARE(session.closeCalls, 1);
        QVERIFY(clock.elapsed() < 5000);
    }

    // An answer still coming in keeps the session open past the linger, and
    // the JSON ends with session.closed even when the server hangs up without
    // one.
    void aSlowAnswerOutlastsTheLinger()
    {
        FakeSession session;
        session.eventGapMs = 150;
        session.sayGoodbye = false;
        session.script = {made(GptLiveEvent::Kind::UserTurn, QStringLiteral("Hello"))};
        for (int word = 0; word < 6; ++word) {
            session.script << made(GptLiveEvent::Kind::AssistantTranscriptDelta, QStringLiteral(" word"));
        }
        session.script << made(GptLiveEvent::Kind::AssistantTurn, QStringLiteral("word word word"));
        LiveOptions options;
        options.input = QStringLiteral("-");
        options.json = true;
        options.lingerMs = 300;
        std::istringstream in(tone(300, 24000, 2400).toStdString());
        std::ostringstream out;
        std::ostringstream err;

        QCOMPARE(runLiveSession(options, session, QStringLiteral("token"), {}, in, out, err), 0);
        const QStringList types = jsonTypes(out.str());
        QVERIFY2(types.contains(QStringLiteral("assistant.turn")), qPrintable(types.join(QLatin1Char(' '))));
        QCOMPARE(types.last(), QStringLiteral("session.closed"));
        QCOMPARE(types.count(QStringLiteral("session.closed")), 1);
        const QString last = QString::fromStdString(out.str()).trimmed().section(QLatin1Char('\n'), -1);
        QCOMPARE(QJsonDocument::fromJson(last.toUtf8()).object().value(QStringLiteral("reason")).toString(),
                 QStringLiteral("connection_closed"));
    }

    // Opt-in, like liveCodexDictationProvider: a real GPT-Live call with the
    // Codex ChatGPT login, the WAV as the caller. The suites run on empty
    // sign-in homes, so the login's folder is named outright.
    void liveGptLiveCall()
    {
        const QString wav = qEnvironmentVariable("SPEECHER_TEST_LIVE_GPT_LIVE_WAV");
        const QString codexHome = qEnvironmentVariable("SPEECHER_TEST_LIVE_CODEX_HOME");
        if (wav.isEmpty() || codexHome.isEmpty()) {
            QSKIP("Live GPT-Live check is opt-in: set SPEECHER_TEST_LIVE_GPT_LIVE_WAV to a WAV of speech and "
                  "SPEECHER_TEST_LIVE_CODEX_HOME to the Codex login's folder, such as ~/.codex");
        }
#ifdef SPEECHER_WITH_GPT_LIVE
        const QByteArray isolatedHome = qgetenv("CODEX_HOME");
        qputenv("CODEX_HOME", QFile::encodeName(codexHome));
        const auto restore = qScopeGuard([&isolatedHome] { qputenv("CODEX_HOME", isolatedHome); });
        const OpenAiAuth auth = OpenAiAuthProvider::forSpeech(SettingsStore().snapshot().accounts).resolve();
        QVERIFY2(auth.ok && auth.chatgptBackend, qPrintable(auth.status));
        LiveOptions options;
        options.input = wav;
        options.json = true;
        options.session.instructions = QStringLiteral("Answer in one short sentence.");
        GptLiveCall call;
        std::istringstream in;
        std::ostringstream out;
        std::ostringstream err;
        QCOMPARE(runLiveSession(options, call, auth.bearerToken, auth.accountId, in, out, err), 0);
        QString userTurn;
        QString assistantTurn;
        qsizetype audioChars = 0;
        for (const QString &line : QString::fromStdString(out.str()).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const QJsonObject event = QJsonDocument::fromJson(line.toUtf8()).object();
            const QString type = event.value(QStringLiteral("type")).toString();
            if (type == QStringLiteral("user.turn")) userTurn += event.value(QStringLiteral("text")).toString();
            if (type == QStringLiteral("assistant.turn")) assistantTurn += event.value(QStringLiteral("text")).toString();
            if (type == QStringLiteral("assistant.audio")) audioChars += event.value(QStringLiteral("pcm_s16le_24k")).toString().size();
        }
        QVERIFY2(!userTurn.isEmpty(), err.str().c_str());
        QVERIFY2(!assistantTurn.isEmpty(), err.str().c_str());
        QVERIFY(audioChars > 0);
        qInfo().noquote() << "user:" << userTurn << "| assistant:" << assistantTurn;
#else
        QSKIP("This build has no GPT-Live");
#endif
    }
};

int runGptLiveTests(int argc, char **argv)
{
    GptLiveTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_gpt_live.moc"
