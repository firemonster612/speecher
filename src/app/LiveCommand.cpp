#include "app/LiveCommand.h"

#include "app/HeadlessTranscribe.h"
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

#include <istream>
#include <memory>
#include <mutex>
#include <ostream>
#include <thread>

namespace speecher {
namespace {

// How far ahead of the call a file is decoded.
constexpr qint64 kReadAheadMs = 2000;
// How often the input is moved on and the end looked for.
constexpr int kTickMs = 50;
// After the assistant answered, how long the user must stay quiet before the
// session counts as finished: the last words can still be in flight.
constexpr qint64 kQuietAfterAnswerMs = 2000;
// How much stdin a read takes.
constexpr std::streamsize kStdinChunkBytes = 4800;

// stdin, read on a thread of its own: a read blocks for as long as the pipe
// stays open, so the run never waits for it to return.
struct StdinQueue {
    std::mutex mutex;
    QByteArray pending;
    bool ended = false;
};

void readStdin(std::istream &in, const std::shared_ptr<StdinQueue> &queue)
{
    char buffer[kStdinChunkBytes];
    while (in.read(buffer, sizeof buffer) || in.gcount() > 0) {
        const std::lock_guard lock(queue->mutex);
        queue->pending.append(buffer, qsizetype(in.gcount()));
    }
    const std::lock_guard lock(queue->mutex);
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

} // namespace

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
        return std::nullopt;
    case Kind::SessionStarted:
        return typed("session.started");
    case Kind::UserTranscriptDelta:
        return withText("user.transcript.delta");
    case Kind::UserTurn:
        return withText("user.turn");
    case Kind::AssistantTranscriptDelta:
        return withText("assistant.transcript.delta");
    case Kind::AssistantTurn:
        return withText("assistant.turn");
    case Kind::AssistantAudio: {
        if (!withAudio) {
            return std::nullopt;
        }
        QJsonObject object = typed("assistant.audio");
        object.insert(QStringLiteral("pcm_s16le_24k"), QString::fromLatin1(event.pcm.toBase64()));
        return object;
    }
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
                   const QString &accountId, std::istream &in, std::ostream &out, std::ostream &err)
{
    const bool audioToStdout = options.audioOut == kStdinFile;
    // With the audio on stdout, the conversation's lines go to stderr.
    std::ostream &lines = audioToStdout ? err : out;
    WavWriter wav;
    if (!options.audioOut.isEmpty() && !audioToStdout) {
        QString error;
        if (!wav.open(options.audioOut, &error)) {
            err << error.toStdString() << "\n";
            return 1;
        }
    }
    const bool wavOpen = !options.audioOut.isEmpty() && !audioToStdout;

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

    const auto fail = [&](const QString &message) {
        err << message.toStdString() << "\n" << std::flush;
        exitCode = 1;
    };
    const auto requestClose = [&] {
        if (!std::exchange(closeRequested, true)) {
            session.close();
        }
    };

    // The input, as 16 kHz mono, by way of the decoder for a file and the
    // stdin queue for raw PCM.
    AudioPcmConverter converter;
    QAudioFormat decodedFormat;
    std::unique_ptr<QAudioDecoder> decoder;
    std::shared_ptr<StdinQueue> stdinQueue;
    std::thread stdinReader;
    // All of the input has been read.
    bool inputRead = false;
    const auto send = [&](const QByteArray &pcm) {
        const AudioPcmConversion converted = converter.convert(pcm);
        if (!converted.error.isEmpty()) {
            fail(QStringLiteral("Could not read the audio: %1").arg(converted.error));
            requestClose();
            return;
        }
        session.sendAudio(converted.pcm16Mono16k);
    };
    if (options.input == kStdinFile) {
        QAudioFormat format;
        format.setSampleRate(options.stdinRate);
        format.setChannelCount(1);
        format.setSampleFormat(QAudioFormat::Int16);
        converter.reset(format);
        stdinQueue = std::make_shared<StdinQueue>();
        stdinReader = std::thread(readStdin, std::ref(in), stdinQueue);
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
            bool ended = false;
            {
                const std::lock_guard lock(stdinQueue->mutex);
                pcm.swap(stdinQueue->pending);
                ended = stdinQueue->ended;
            }
            if (!pcm.isEmpty()) {
                send(pcm);
            }
            inputRead = ended;
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
        if (!inputEnded && inputRead && session.queuedMs() == 0) {
            inputEnded = true;
            sinceInputSent.start();
        }
        if (inputEnded
            && ((answered && sinceUser.elapsed() >= kQuietAfterAnswerMs && sinceInputSent.elapsed() >= kQuietAfterAnswerMs)
                || sinceInputSent.elapsed() >= options.lingerMs)) {
            requestClose();
        }
    };
    QTimer ticker;
    ticker.setInterval(kTickMs);
    QObject::connect(&ticker, &QTimer::timeout, &loop, tick);
    QObject::connect(&session, &GptLiveSession::event, &loop, [&](const GptLiveEvent &event) {
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
            answered = false;
            sinceUser.start();
            break;
        case Kind::AssistantTurn:
            answered = true;
            break;
        case Kind::AssistantAudio:
            if (audioToStdout) {
                out.write(event.pcm.constData(), event.pcm.size());
                out.flush();
            } else if (wavOpen) {
                wav.append(event.pcm);
            }
            break;
        case Kind::SessionClosed:
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
        if (options.json) {
            if (const std::optional<QJsonObject> line = liveEventJson(event, options.audioOut.isEmpty())) {
                writeLine(out, *line);
            }
        } else if (event.kind == Kind::UserTurn || event.kind == Kind::AssistantTurn) {
            lines << (event.kind == Kind::UserTurn ? "user: " : "assistant: ") << event.text.toStdString() << "\n"
                  << std::flush;
        }
    });
    QObject::connect(&session, &GptLiveSession::failed, &loop, [&](const QString &message) { fail(message); });
    QObject::connect(&session, &GptLiveSession::closed, &loop, [&] { loop.quit(); });

    err << "Connecting to GPT-Live\n" << std::flush;
    session.start(accessToken, accountId, options.session);
    loop.exec();

    if (stdinReader.joinable()) {
        const std::lock_guard lock(stdinQueue->mutex);
        // A pipe still open would hold a join for good; the process ends soon.
        if (stdinQueue->ended) {
            stdinReader.join();
        } else {
            stdinReader.detach();
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

int runLiveCommand(const LiveOptions &options, SettingsStore *settings, std::istream &in, std::ostream &out,
                   std::ostream &err)
{
#ifdef SPEECHER_WITH_GPT_LIVE
    const OpenAiAuth auth = OpenAiAuthProvider::forSpeech(settings->dictationSnapshot().accounts).resolve();
    if (!auth.ok || !auth.chatgptBackend) {
        err << "GPT-Live needs the ChatGPT sign-in the Codex CLI or the ChatGPT app makes"
            << (auth.status.isEmpty() ? std::string() : ": " + auth.status.toStdString()) << "\n";
        return 1;
    }
    GptLiveCall call;
    return runLiveSession(options, call, auth.bearerToken, auth.accountId, in, out, err);
#else
    Q_UNUSED(options);
    Q_UNUSED(settings);
    Q_UNUSED(in);
    Q_UNUSED(out);
    err << "This Speecher was built without GPT-Live, so it has no speecher live.\n";
    return 1;
#endif
}

} // namespace speecher
