#include "app/LiveCommand.h"

#include "app/HeadlessTranscribe.h"
#include "app/PlatformComposition.h"
#include "core/EchoCanceller.h"
#include "core/SettingsStore.h"
#include "platform/audio/AudioPcmConverter.h"
#include "providers/GptLiveSession.h"
#include "providers/OpenAiAuthProvider.h"
#ifdef SPEECHER_WITH_GPT_LIVE
#include "providers/GptLiveCall.h"
#endif

#include <QAudioDecoder>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QTimer>
#include <QUrl>
#include <QtEndian>

#include <algorithm>
#ifdef Q_OS_WIN
#include <io.h>
#else
#include <cerrno>
#include <unistd.h>
#endif
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <thread>

namespace speecher {
namespace {

// How far ahead of the call a file is decoded.
constexpr qint64 kReadAheadMs = 2000;
// How often the input is moved on and the end looked for.
constexpr int kTickMs = 20;
// After the assistant answered, how long the user must stay quiet before the
// session counts as finished: the last words can still be in flight.
constexpr qint64 kQuietAfterAnswerMs = 2000;
// How much raw stdin a read takes.
constexpr std::streamsize kStdinChunkBytes = 4800;

// stdin, read on a thread of its own: a read blocks for as long as the pipe
// stays open, so the run never waits for it to return. Raw PCM gathers in
// pending; the host protocol's commands come a line each.
struct StdinQueue {
    std::mutex mutex;
    QByteArray pending;
    QList<QByteArray> lines;
    bool ended = false;
};

void readStdin(const LiveInputReader &read, const std::shared_ptr<StdinQueue> &queue, bool byLine)
{
    char buffer[kStdinChunkBytes];
    QByteArray partLine;
    for (qint64 got; (got = read(buffer, sizeof buffer)) > 0;) {
        const std::lock_guard lock(queue->mutex);
        if (!byLine) {
            queue->pending.append(buffer, qsizetype(got));
            continue;
        }
        partLine.append(buffer, qsizetype(got));
        for (qsizetype end; (end = partLine.indexOf('\n')) >= 0;) {
            queue->lines.append(partLine.left(end));
            partLine.remove(0, end + 1);
        }
    }
    const std::lock_guard lock(queue->mutex);
    if (!partLine.isEmpty()) {
        queue->lines.append(partLine);
    }
    queue->ended = true;
}

// A WAV file written as the audio comes, its sizes filled in at the end.
class WavWriter {
public:
    bool open(const QString &path, QString *error)
    {
        m_file.setFileName(path);
        if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate) || m_file.write(header(0)) != 44) {
            *error = QStringLiteral("Cannot write %1: %2").arg(path, m_file.errorString());
            return false;
        }
        return true;
    }
    void append(const QByteArray &pcm)
    {
        m_file.write(pcm);
        m_bytes += pcm.size();
    }
    // False when the file did not take all of it.
    bool finish()
    {
        return m_file.seek(0) && m_file.write(header(m_bytes)) == 44 && m_file.flush()
            && m_file.error() == QFileDevice::NoError;
    }

private:
    static QByteArray header(quint32 dataBytes)
    {
        QByteArray out;
        const auto u32 = [&out](quint32 value) {
            char bytes[4];
            qToLittleEndian(value, bytes);
            out.append(bytes, 4);
        };
        const auto u16 = [&out](quint16 value) {
            char bytes[2];
            qToLittleEndian(value, bytes);
            out.append(bytes, 2);
        };
        out.append("RIFF");
        u32(36 + dataBytes);
        out.append("WAVEfmt ");
        u32(16);
        u16(1);
        u16(1);
        u32(kGptLiveOutputSampleRate);
        u32(kGptLiveOutputSampleRate * 2);
        u16(2);
        u16(16);
        out.append("data");
        u32(dataBytes);
        return out;
    }

    QFile m_file;
    quint32 m_bytes = 0;
};

void writeLine(std::ostream &out, const QJsonObject &object)
{
    out << QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString() << '\n' << std::flush;
}

QJsonObject timed(QJsonObject object, const GptLiveEvent &event)
{
    if (event.startMs >= 0) {
        object.insert(QStringLiteral("start_ms"), event.startMs);
    }
    if (event.endMs >= 0) {
        object.insert(QStringLiteral("end_ms"), event.endMs);
    }
    return object;
}

bool silent(const QByteArray &pcm)
{
    return std::all_of(pcm.cbegin(), pcm.cend(), [](char byte) { return byte == 0; });
}

QAudioFormat monoInt16(int sampleRate)
{
    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    return format;
}

} // namespace

LiveInputReader stdinReader()
{
    return [](char *buffer, qint64 size) -> qint64 {
        for (;;) {
#ifdef Q_OS_WIN
            const int got = _read(0, buffer, unsigned(size));
#else
            const ssize_t got = ::read(STDIN_FILENO, buffer, size_t(size));
            if (got < 0 && errno == EINTR) {
                continue;
            }
#endif
            return got < 0 ? 0 : qint64(got);
        }
    };
}

std::optional<QJsonObject> liveEventJson(const GptLiveEvent &event, bool withAudio)
{
    using Kind = GptLiveEvent::Kind;
    const auto typed = [](const char *type) { return QJsonObject{{QStringLiteral("type"), QLatin1String(type)}}; };
    const auto withText = [&](const char *type) {
        QJsonObject object = typed(type);
        object.insert(QStringLiteral("text"), event.text);
        return timed(object, event);
    };
    switch (event.kind) {
    case Kind::Ignored:
    case Kind::InputEcho:
        return std::nullopt;
    case Kind::SessionStarted:
        return QJsonObject{{QStringLiteral("type"), QStringLiteral("session.started")},
                           {QStringLiteral("protocol"), kLiveProtocolVersion},
                           {QStringLiteral("output_rate"), kGptLiveOutputSampleRate}};
    case Kind::UserTurnStarted:
        return timed(typed("user.speech_started"), event);
    case Kind::UserTranscriptDelta:
        return withText("user.transcript.delta");
    case Kind::UserTurn:
        return withText("user.turn");
    case Kind::AssistantTurnStarted:
        return timed(typed("assistant.speech_started"), event);
    case Kind::AssistantTranscriptDelta:
        return withText("assistant.transcript.delta");
    case Kind::AssistantTurn:
        return withText("assistant.turn");
    case Kind::AssistantInterrupted: {
        QJsonObject object = withText("assistant.interrupted");
        object.insert(QStringLiteral("heard_ms"), event.audioMs);
        return object;
    }
    case Kind::AssistantAudio: {
        if (!withAudio || silent(event.pcm)) {
            return std::nullopt;
        }
        QJsonObject object = typed("assistant.audio");
        object.insert(QStringLiteral("pcm_s16le_24k"), QString::fromLatin1(event.pcm.toBase64()));
        object.insert(QStringLiteral("ms"), event.pcm.size() * 1000 / (kGptLiveOutputSampleRate * 2));
        return object;
    }
    case Kind::ContextAppended:
        return typed("context.added");
    case Kind::InputPaused:
        return typed("input.muted");
    case Kind::InputResumed:
        return typed("input.unmuted");
    case Kind::Usage: {
        QJsonObject object = typed("usage");
        object.insert(QStringLiteral("audio_ms"), event.audioMs);
        return object;
    }
    case Kind::SessionClosed: {
        QJsonObject object = typed("session.closed");
        object.insert(QStringLiteral("reason"), event.text);
        return object;
    }
    case Kind::Error: {
        QJsonObject object = typed("error");
        object.insert(QStringLiteral("message"), event.text);
        return object;
    }
    }
    return std::nullopt;
}

int runLiveSession(const LiveOptions &options, GptLiveSession &session, const QString &accessToken,
                   const QString &accountId, const LiveInputReader &in, std::ostream &out, std::ostream &err,
                   EchoCanceller *echo)
{
    const bool json = options.json || options.hostProtocol;
    const bool audioToStdout = options.audioOut == kStdinFile;
    // With the audio on stdout, the conversation's lines go to stderr.
    std::ostream &lines = audioToStdout ? err : out;
    WavWriter wav;
    const bool wavOpen = !options.audioOut.isEmpty() && !audioToStdout;
    if (wavOpen) {
        QString error;
        if (!wav.open(options.audioOut, &error)) {
            err << error.toStdString() << "\n";
            return 1;
        }
    }

    QEventLoop loop;
    int exitCode = 0;
    bool closeRequested = false;
    // The input has all reached the session, and it has sent it all on.
    bool inputEnded = false;
    QElapsedTimer sinceInputSent;
    // Whether the assistant has answered the latest thing said, and when the
    // user last said anything.
    bool answered = false;
    QElapsedTimer sinceUser;
    sinceUser.start();
    // When either side last said anything: a slow answer still coming in
    // keeps the session open past the linger.
    QElapsedTimer sinceSpeech;
    sinceSpeech.start();
    // --json ends with one session.closed, whether or not the server sent it.
    bool closedSeen = false;
    // The host's playback.stop: the rest of the assistant's turn is dropped.
    bool playbackStopped = false;
    GptLiveTurnTracker turns;

    const auto fail = [&](const QString &message) {
        err << message.toStdString() << "\n" << std::flush;
        exitCode = 1;
    };
    const auto requestClose = [&] {
        if (!std::exchange(closeRequested, true)) {
            session.close();
        }
    };
    const auto emitJson = [&](const QJsonObject &object) {
        if (json) {
            writeLine(out, object);
        }
    };
    const auto hostError = [&](const QString &message) {
        emitJson({{QStringLiteral("type"), QStringLiteral("error")}, {QStringLiteral("message"), message}});
    };

    // The input, as 16 kHz mono: by way of the decoder for a file, and of the
    // stdin queue for raw PCM or the host's input.audio.
    AudioPcmConverter converter;
    // The assistant's audio as 16 kHz mono, the echo canceller's reference.
    AudioPcmConverter reference;
    reference.reset(monoInt16(kGptLiveOutputSampleRate));
    QAudioFormat decodedFormat;
    std::unique_ptr<QAudioDecoder> decoder;
    std::shared_ptr<StdinQueue> stdinQueue;
    std::thread inputThread;
    // All of the input has been read.
    bool inputRead = false;
    const auto send = [&](const QByteArray &pcm) {
        const AudioPcmConversion converted = converter.convert(pcm);
        if (!converted.error.isEmpty()) {
            fail(QStringLiteral("Could not read the audio: %1").arg(converted.error));
            requestClose();
            return;
        }
        session.sendAudio(echo ? echo->process(converted.pcm16Mono16k) : converted.pcm16Mono16k);
    };
    const auto applyCommand = [&](const QByteArray &line) {
        if (line.trimmed().isEmpty()) {
            return;
        }
        const QJsonObject command = QJsonDocument::fromJson(line).object();
        const QString type = command.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("input.audio")) {
            send(QByteArray::fromBase64(command.value(QStringLiteral("audio")).toString().toLatin1()));
        } else if (type == QStringLiteral("input.mute") || type == QStringLiteral("input.unmute")) {
            session.setInputPaused(type == QStringLiteral("input.mute"));
        } else if (type == QStringLiteral("context.add")) {
            session.sendContext(command.value(QStringLiteral("text")).toString());
        } else if (type == QStringLiteral("playback.stop")) {
            playbackStopped = true;
        } else if (type == QStringLiteral("session.end")) {
            requestClose();
        } else {
            hostError(type.isEmpty() ? QStringLiteral("Not a command: %1").arg(QString::fromUtf8(line.left(80)))
                                     : QStringLiteral("Unknown command: %1").arg(type));
        }
    };
    if (options.hostProtocol || options.input == kStdinFile) {
        converter.reset(monoInt16(options.stdinRate));
        stdinQueue = std::make_shared<StdinQueue>();
        inputThread = std::thread(readStdin, in, stdinQueue, options.hostProtocol);
    } else {
        decoder = std::make_unique<QAudioDecoder>();
        decoder->setSource(QUrl::fromLocalFile(options.input));
        QObject::connect(decoder.get(), &QAudioDecoder::finished, &loop, [&] { inputRead = true; });
        QObject::connect(decoder.get(), qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), &loop, [&] {
            fail(QStringLiteral("Could not decode %1: %2").arg(options.input, decoder->errorString()));
            inputRead = true;
            requestClose();
        });
    }
    // Moves the input on, and closes once the conversation is over.
    const auto tick = [&] {
        if (closeRequested) {
            return;
        }
        if (stdinQueue) {
            QByteArray pcm;
            QList<QByteArray> commands;
            {
                const std::lock_guard lock(stdinQueue->mutex);
                pcm.swap(stdinQueue->pending);
                commands.swap(stdinQueue->lines);
                inputRead = stdinQueue->ended;
            }
            if (!pcm.isEmpty()) {
                send(pcm);
            }
            for (const QByteArray &command : std::as_const(commands)) {
                applyCommand(command);
            }
        } else {
            while (decoder->bufferAvailable() && session.queuedMs() < kReadAheadMs) {
                const QAudioBuffer buffer = decoder->read();
                if (buffer.format() != decodedFormat) {
                    decodedFormat = buffer.format();
                    converter.reset(decodedFormat);
                }
                send(QByteArray(buffer.constData<char>(), buffer.byteCount()));
            }
        }
        // The host protocol goes on until the host or stdin ends it.
        if (options.hostProtocol) {
            if (inputRead) {
                requestClose();
            }
            return;
        }
        if (!inputEnded && inputRead && session.queuedMs() == 0) {
            inputEnded = true;
            sinceInputSent.start();
        }
        const bool answeredAndQuiet = answered && sinceUser.elapsed() >= kQuietAfterAnswerMs
            && sinceInputSent.elapsed() >= kQuietAfterAnswerMs;
        const bool idle = sinceInputSent.elapsed() >= options.lingerMs && sinceSpeech.elapsed() >= options.lingerMs;
        if (inputEnded && (answeredAndQuiet || idle)) {
            requestClose();
        }
    };
    QTimer ticker;
    ticker.setInterval(kTickMs);
    QObject::connect(&ticker, &QTimer::timeout, &loop, tick);

    const auto handle = [&](const GptLiveEvent &event) {
        using Kind = GptLiveEvent::Kind;
        switch (event.kind) {
        case Kind::SessionStarted:
            // The input goes out once the session is there to take it.
            if (!ticker.isActive()) {
                if (decoder) {
                    decoder->start();
                }
                ticker.start();
            }
            break;
        case Kind::UserTranscriptDelta:
        case Kind::UserTurn:
        case Kind::UserTurnStarted:
            answered = false;
            sinceUser.start();
            sinceSpeech.start();
            break;
        case Kind::AssistantTurnStarted:
            playbackStopped = false;
            sinceSpeech.start();
            break;
        case Kind::AssistantTranscriptDelta:
            sinceSpeech.start();
            break;
        case Kind::AssistantTurn:
            answered = true;
            sinceSpeech.start();
            break;
        case Kind::AssistantAudio:
            if (echo) {
                echo->addReference(reference.convert(event.pcm).pcm16Mono16k);
            }
            if (playbackStopped) {
                return;
            }
            if (audioToStdout) {
                out.write(event.pcm.constData(), event.pcm.size());
                out.flush();
            } else if (wavOpen) {
                wav.append(event.pcm);
            }
            break;
        case Kind::SessionClosed:
            closedSeen = true;
            if (!closeRequested) {
                fail(QStringLiteral("GPT-Live ended the session: %1").arg(event.text));
            }
            break;
        case Kind::Error:
            err << "GPT-Live: " << event.text.toStdString() << "\n" << std::flush;
            break;
        default:
            break;
        }
        if (json) {
            if (std::optional<QJsonObject> line = liveEventJson(event, options.audioOut.isEmpty())) {
                if (event.kind == Kind::SessionStarted) {
                    line->insert(QStringLiteral("input_rate"), options.stdinRate);
                }
                writeLine(out, *line);
            }
        } else if (event.kind == Kind::UserTurn || event.kind == Kind::AssistantTurn) {
            lines << (event.kind == Kind::UserTurn ? "user: " : "assistant: ") << event.text.toStdString() << "\n"
                  << std::flush;
        } else if (event.kind == Kind::AssistantInterrupted) {
            lines << "(the user talked over the assistant " << event.audioMs << " ms into its turn)\n" << std::flush;
        }
    };
    QObject::connect(&session, &GptLiveSession::event, &loop, [&](const GptLiveEvent &event) {
        handle(event);
        if (const std::optional<GptLiveEvent> interrupted = turns.take(event)) {
            handle(*interrupted);
        }
    });
    QObject::connect(&session, &GptLiveSession::failed, &loop, [&](const QString &message) { fail(message); });
    QObject::connect(&session, &GptLiveSession::closed, &loop, [&] { loop.quit(); });

    err << "Connecting to GPT-Live\n" << std::flush;
    session.start(accessToken, accountId, options.session);
    loop.exec();
    // The server may hang up without a goodbye.
    if (json && !closedSeen) {
        writeLine(out, *liveEventJson({GptLiveEvent::Kind::SessionClosed, QStringLiteral("connection_closed")}, false));
    }

    if (inputThread.joinable()) {
        const std::lock_guard lock(stdinQueue->mutex);
        // A pipe still open would hold a join for good; the process ends soon.
        if (stdinQueue->ended) {
            inputThread.join();
        } else {
            inputThread.detach();
        }
    }
    if (wavOpen && !wav.finish()) {
        fail(QStringLiteral("Could not finish %1").arg(options.audioOut));
    }
    if (!out) {
        fail(QStringLiteral("Could not write to stdout"));
    }
    return exitCode;
}

int runLiveCommand(const LiveOptions &options, SettingsStore *settings,
                   const std::shared_ptr<const PlatformComposition> &platform, std::ostream &out, std::ostream &err)
{
#ifdef SPEECHER_WITH_GPT_LIVE
    const OpenAiAuth auth = OpenAiAuthProvider::forSpeech(settings->dictationSnapshot().accounts).resolve();
    if (!auth.ok || !auth.chatgptBackend) {
        err << "GPT-Live needs the ChatGPT sign-in the Codex CLI or the ChatGPT app makes"
            << (auth.status.isEmpty() ? std::string() : ": " + auth.status.toStdString()) << "\n";
        return 1;
    }
    std::unique_ptr<EchoCanceller> echo;
    if (options.echoCancel) {
        QString warning;
        echo = platform->createEchoCanceller(&warning);
        if (!echo) {
            err << "This build cannot cancel echo"
                << (warning.isEmpty() ? std::string() : ": " + warning.toStdString()) << "\n";
            return 1;
        }
    }
    GptLiveCall call;
    return runLiveSession(options, call, auth.bearerToken, auth.accountId, stdinReader(), out, err, echo.get());
#else
    Q_UNUSED(options);
    Q_UNUSED(settings);
    Q_UNUSED(platform);
    Q_UNUSED(out);
    err << "This Speecher was built without GPT-Live, so it has no speecher live.\n";
    return 1;
#endif
}

} // namespace speecher
