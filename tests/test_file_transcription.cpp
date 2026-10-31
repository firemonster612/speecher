#include "common/test_suites.h"
#include "common/test_doubles.h"
#include "common/test_http.h"

#include "app/ApplicationController.h"
#include "app/HeadlessTranscribe.h"
#include "core/EchoCanceller.h"
#include "core/SettingsStore.h"
#include "core/VocabularyLimit.h"
#include "core/settings/SettingsKeys.h"
#include "dictation/DictationSession.h"
#include "providers/EndpointSpeechTranscriber.h"
#include "providers/SpeechChain.h"
#include "recording/RecordingPresentation.h"
#include "recording/RecordingSession.h"
#include "recording/SystemAudioPresentation.h"
#include "transcribe/FileTranscriptionSession.h"
#include "transcribe/Subtitles.h"
#include "transcribe/TranscribePresentation.h"
#include "transcribe/TranscribeQueue.h"

#include <QAudioDecoder>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaFormat>
#include <QMimeDatabase>
#include <QPointer>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtEndian>
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
#include <QAudioBufferInput>
#include <QImage>
#include <QMediaCaptureSession>
#include <QMediaRecorder>
#include <QVideoFrame>
#include <QVideoFrameInput>
#endif

#include <algorithm>
#include <cmath>
#include <csignal>
#include <functional>
#include <optional>
#include <sstream>
#include <utility>
#ifdef Q_OS_UNIX
#include <sys/resource.h>
#endif

using namespace speecher;
using namespace speecher::test;

namespace {

// What the scripted transcribers saw. Owned by the test, because the session
// deletes each file's transcriber once the file is done.
struct Script {
    bool expireFirstStream = false;
    // What the first stream received before it ended, with expireFirstStream.
    qsizetype firstStreamBytes = 0;
    int attempts = 0;
    qsizetype bytes = 0;
    QStringList vocabulary;
    QString language;
    bool timedSegments = false;
    // Sent with each finished input, timed from the start of its attempt.
    QList<TranscriptSegment> segments;
    // Called with each chunk of audio sent.
    std::function<void()> audioSent;
    // How long each preparation takes on the worker.
    int prepareDelayMs = 0;
    // Each attempt connects as it starts, as the Custom Endpoint's does.
    bool connects = false;
};

// Answers a finished input with how many bytes of audio it received, and can
// end its first stream early the way a Codex session expires mid-file.
class ScriptedTranscriber final : public SpeechTranscriber {
public:
    ScriptedTranscriber(Script *script, QObject *parent)
        : SpeechTranscriber(parent)
        , m_script(script)
    {
    }

    bool requiresRefresh(const SpeechSettings &, const AccountSettings &) const override { return false; }
    std::optional<ProviderFailure> prepare(const SpeechSettings &, const AccountSettings &) override
    {
        return std::nullopt;
    }
    std::optional<PreparationJob> createPrepareJob(const SpeechSettings &, const AccountSettings &) override
    {
        if (m_script->prepareDelayMs == 0) {
            return std::nullopt;
        }
        return PreparationJob{.run = [ms = m_script->prepareDelayMs]() -> std::optional<ProviderFailure> {
            QThread::msleep(ms);
            return std::nullopt;
        }};
    }

    void startAttempt(quint64 attemptId, const SpeechSettings &settings, const SpeechAttemptOptions &options) override
    {
        m_attemptId = attemptId;
        ++m_script->attempts;
        m_script->vocabulary = settings.vocabulary;
        m_script->language = settings.language;
        m_script->timedSegments = options.timedSegments;
        m_bytes = 0;
        if (m_script->connects) {
            emit attemptConnected(attemptId);
        }
    }

    // Audio for an attempt this transcriber is not on goes nowhere.
    void sendAudio(quint64 attemptId, const QByteArray &pcm) override
    {
        if (attemptId != m_attemptId) {
            return;
        }
        m_script->bytes += pcm.size();
        m_bytes += pcm.size();
        if (m_script->audioSent) {
            m_script->audioSent();
        }
        if (m_script->expireFirstStream && m_script->attempts == 1) {
            m_script->firstStreamBytes += pcm.size();
            emit finalTranscript(attemptId, QStringLiteral("before"));
            emit attemptCompleted(attemptId);
        }
    }

    void finishInput(quint64 attemptId) override
    {
        if (!m_script->segments.isEmpty()) {
            emit attemptSegments(attemptId, m_script->segments);
        }
        emit finalTranscript(attemptId, QStringLiteral("heard %1").arg(m_bytes));
        emit attemptCompleted(attemptId);
    }

    void cancelAttempt(quint64) override {}

private:
    Script *m_script;
    quint64 m_attemptId = 0;
    qsizetype m_bytes = 0;
};

// A 440 Hz tone, half a second unless ms says, as 44.1 kHz stereo s16, so
// the file has to be downmixed and resampled on its way to the provider.
void writeWav(const QString &path, int ms = 500)
{
    constexpr int rate = 44100;
    constexpr int channels = 2;
    QByteArray data;
    for (int i = 0; i < rate * ms / 1000; ++i) {
        const auto sample = qint16(8000 * std::sin(2 * M_PI * 440 * i / rate));
        for (int c = 0; c < channels; ++c) {
            data.append(reinterpret_cast<const char *>(&sample), 2);
        }
    }
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(wavBytes(data, rate, channels));
}

// 100 ms of 16 kHz mono s16 at one level, as a microphone delivers it: 0 is
// silence, 8000 is well above Skip silence's default threshold.
QByteArray microphoneChunk(qint16 level)
{
    QByteArray chunk;
    for (int i = 0; i < 1600; ++i) {
        chunk.append(reinterpret_cast<const char *>(&level), 2);
    }
    return chunk;
}

// How much audio an upload to the Custom Endpoint carries: its WAV's data
// size.
qsizetype uploadedAudioBytes(const QByteArray &request)
{
    const qsizetype data = request.indexOf("data", request.indexOf("WAVE"));
    return qFromLittleEndian<quint32>(request.constData() + data + 4);
}

qsizetype byteCount(const QList<QByteArray> &chunks)
{
    qsizetype bytes = 0;
    for (const QByteArray &chunk : chunks) {
        bytes += chunk.size();
    }
    return bytes;
}

QString readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
}

QByteArray fileBytes(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// The text of each "[hh:mm:ss] me: text" line of a recording; a line in any
// other shape is kept whole, so a comparison shows it.
QStringList recordedTexts(const QString &path)
{
    static const QRegularExpression line(QStringLiteral("^\\[\\d\\d:\\d\\d:\\d\\d\\] me: (.+)$"));
    QStringList texts;
    for (const QString &written : readFile(path).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QRegularExpressionMatch match = line.match(written);
        texts << (match.hasMatch() ? match.captured(1) : written);
    }
    return texts;
}

// Each line of a recording as "speaker: text", without its time.
QStringList recordedLines(const QString &path)
{
    QStringList lines = readFile(path).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (QString &line : lines) {
        line.remove(QRegularExpression(QStringLiteral("^\\[\\d\\d:\\d\\d:\\d\\d\\] ")));
    }
    return lines;
}

// What a FakeEchoCanceller was given, kept by the test, as the recording
// destroys the canceller once it is flushed.
struct EchoCancellerLog {
    QList<QByteArray> references;
    int flushes = 0;
};

// Holds the microphone's last delayBytes back, as a canceller holds audio
// until the system audio it echoes has arrived, and logs the rest.
class FakeEchoCanceller final : public EchoCanceller {
public:
    explicit FakeEchoCanceller(EchoCancellerLog *log, qsizetype delayBytes = 0)
        : m_log(log)
        , m_delayBytes(delayBytes)
    {
    }

    void addReference(const QByteArray &pcm) override { m_log->references << pcm; }
    QByteArray process(const QByteArray &microphonePcm) override
    {
        m_held += microphonePcm;
        const QByteArray released = m_held.left(std::max<qsizetype>(0, m_held.size() - m_delayBytes));
        m_held.remove(0, released.size());
        return released;
    }
    QByteArray flush() override
    {
        ++m_log->flushes;
        return std::exchange(m_held, {});
    }

private:
    EchoCancellerLog *m_log;
    qsizetype m_delayBytes;
    QByteArray m_held;
};

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
// Records half a second of 440 Hz tone beside a few plain video frames into
// path with Qt's own FFmpeg recorder (its audio and video inputs arrived in
// Qt 6.8), so the test needs no fixture files. Returns why it could not, or
// empty once the file is written.
QString recordClip(const QString &path, QMediaFormat::FileFormat container,
                   QMediaFormat::AudioCodec audioCodec, QMediaFormat::VideoCodec videoCodec)
{
    constexpr int rate = 44100;
    constexpr int samples = rate / 2;
    constexpr int frames = 5;
    QMediaCaptureSession session;
    QAudioBufferInput audio;
    QVideoFrameInput video;
    QMediaRecorder recorder;
    session.setAudioBufferInput(&audio);
    session.setVideoFrameInput(&video);
    session.setRecorder(&recorder);
    QMediaFormat format(container);
    format.setAudioCodec(audioCodec);
    format.setVideoCodec(videoCodec);
    recorder.setMediaFormat(format);
    recorder.setOutputLocation(QUrl::fromLocalFile(path));
    recorder.setAutoStop(true);

    QAudioFormat pcm;
    pcm.setSampleRate(rate);
    pcm.setChannelCount(1);
    pcm.setSampleFormat(QAudioFormat::Int16);
    int samplesSent = 0;
    QObject::connect(&audio, &QAudioBufferInput::readyToSendAudioBuffer, &audio, [&] {
        while (samplesSent < samples) {
            QByteArray data;
            const int count = std::min(rate / 10, samples - samplesSent);
            for (int i = 0; i < count; ++i) {
                const auto sample = qint16(8000 * std::sin(2 * M_PI * 440 * (samplesSent + i) / rate));
                data.append(reinterpret_cast<const char *>(&sample), 2);
            }
            if (!audio.sendAudioBuffer(QAudioBuffer(data, pcm, qint64(samplesSent) * 1000000 / rate))) {
                return;
            }
            samplesSent += count;
        }
        audio.sendAudioBuffer({});
    });
    int framesSent = 0;
    QObject::connect(&video, &QVideoFrameInput::readyToSendVideoFrame, &video, [&] {
        while (framesSent < frames) {
            QImage image(320, 240, QImage::Format_RGB32);
            image.fill(Qt::darkGreen);
            QVideoFrame frame(image);
            frame.setStartTime(framesSent * 100000);
            frame.setEndTime((framesSent + 1) * 100000);
            if (!video.sendVideoFrame(frame)) {
                return;
            }
            ++framesSent;
        }
        video.sendVideoFrame({});
    });
    QString error;
    QObject::connect(&recorder, &QMediaRecorder::errorOccurred, &recorder,
                     [&error](QMediaRecorder::Error, const QString &message) { error = message; });
    recorder.record();
    const bool stopped = QTest::qWaitFor(
        [&] { return !error.isEmpty() || recorder.recorderState() == QMediaRecorder::StoppedState; }, 10000);
    if (!error.isEmpty()) {
        return error;
    }
    return stopped && QFileInfo(path).size() > 0 ? QString() : QStringLiteral("the recorder wrote nothing");
}

#endif

// Every first capture of pattern in a file under packaging/, sorted.
QStringList packagingEntries(const QString &file, const QString &pattern)
{
    QFile source(QStringLiteral(SPEECHER_SOURCE_DIR "/packaging/") + file);
    // Text mode: a Windows checkout ends its lines in CRLF, and a stray CR
    // would read as one more MIME type.
    if (!source.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    QStringList entries;
    const QString text = QString::fromUtf8(source.readAll());
    for (const QRegularExpressionMatch &match :
         QRegularExpression(pattern, QRegularExpression::MultilineOption).globalMatch(text)) {
        entries << match.captured(1);
    }
    entries.sort();
    return entries;
}

QStringList sorted(QStringList list)
{
    list.sort();
    return list;
}

// Points the temporary folder at folder until the returned guard ends: Unix
// reads TMPDIR, Windows TEMP and TMP.
auto redirectTemporaryFolder(const QString &folder)
{
    const QList<QByteArray> names{"TMPDIR", "TEMP", "TMP"};
    QList<std::optional<QByteArray>> saved;
    for (const QByteArray &name : names) {
        saved << (qEnvironmentVariableIsSet(name.constData()) ? std::optional(qgetenv(name.constData()))
                                                               : std::nullopt);
        qputenv(name.constData(), QFile::encodeName(QDir::toNativeSeparators(folder)));
    }
    return qScopeGuard([names, saved] {
        for (qsizetype index = 0; index < names.size(); ++index) {
            if (saved.at(index)) {
                qputenv(names.at(index).constData(), *saved.at(index));
            } else {
                qunsetenv(names.at(index).constData());
            }
        }
    });
}

// Stdin that, when first read, records the names of the files under folder,
// where a run spooling it has created its file by then.
class SpoolRecordingInput final : public std::stringbuf {
public:
    SpoolRecordingInput(const std::string &data, const QString &folder)
        : std::stringbuf(data)
        , m_folder(folder)
    {
    }

    QStringList spooled() const { return m_spooled.value_or(QStringList()); }

protected:
    std::streamsize xsgetn(char *buffer, std::streamsize count) override
    {
        if (!m_spooled) {
            m_spooled.emplace();
            QDirIterator files(m_folder, QDir::Files, QDirIterator::Subdirectories);
            while (files.hasNext()) {
                files.next();
                *m_spooled << files.fileName();
            }
        }
        return std::stringbuf::xsgetn(buffer, count);
    }

private:
    QString m_folder;
    std::optional<QStringList> m_spooled;
};


// The queue as the Transcribe page builds it, over the controller's own
// settings, providers and session.
class ControllerQueue : public TranscribeQueue {
public:
    explicit ControllerQueue(ApplicationController &controller)
        : TranscribeQueue(controller.settings(),
                          controller.providerRegistry(),
                          controller.fileTranscription(),
                          [&controller] { return controller.fileTranscriptionRefusal(); })
    {
    }
};

class FileTranscriptionTests : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        SettingsStore().raw().clear();
        QVERIFY(m_dir.isValid());
        m_registry = std::make_unique<ProviderRegistry>();
        m_script = {};
        m_registry->registerSpeechProvider({QStringLiteral("claude"), QStringLiteral("Scripted")},
                                           [this](QObject *parent) {
                                               return new ScriptedTranscriber(&m_script, parent);
                                           });
        m_refinedWith.clear();
        m_refinedVocabulary.clear();
        m_refinedLanguage.clear();
        m_codexes.clear();
        m_registry->registerRefinementProvider({QStringLiteral("openai"), QStringLiteral("Fake")},
                                               [this](QObject *parent) {
                                                   auto *refiner = new FakeRefiner(parent);
                                                   connect(refiner, &TranscriptRefiner::completed, this, [this, refiner] {
                                                       m_refinedWith = {refiner->lastStyle, refiner->lastTone};
                                                       m_refinedVocabulary = refiner->lastVocabulary;
                                                       m_refinedLanguage = refiner->lastContext.spokenLanguage;
                                                   });
                                                   refiner->autoComplete = true;
                                                   refiner->autoCompleteText = QStringLiteral("Heard it.");
                                                   return refiner;
                                               });
    }

    void decodesStreamsRefinesAndSavesBesideTheInput()
    {
        const QString audio = m_dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        SettingsStore settings;
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        TranscribeOptions options = speechOnly();
        options.refinementProviderId = QStringLiteral("openai");
        options.cleanupStrength = QStringLiteral("strong_polish");
        options.tone = QStringLiteral("formal");

        QVERIFY(session.start({audio}, options));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);

        // Half a second at 16 kHz mono s16, give or take the resampler's edge.
        QVERIFY(qAbs(m_script.bytes - 16000) <= 8);
        const auto results = finished.first().first().value<QList<TranscribeFileResult>>();
        QCOMPARE(results.size(), 1);
        QVERIFY(results.first().raw.startsWith(QStringLiteral("heard ")));
        QCOMPARE(m_refinedWith, QStringList({QStringLiteral("strong_polish"), QStringLiteral("formal")}));
        QCOMPARE(results.first().refined, QStringLiteral("Heard it."));
        QCOMPARE(results.first().savedPath, m_dir.filePath(QStringLiteral("memo-transcribed.txt")));
        QCOMPARE(readFile(results.first().savedPath), QStringLiteral("Heard it."));
    }

    // The waveform and length come from a first pass over the file, before
    // any progress; the bar then fills to the end before refinement.
    void filesReportTheirPhasesInOrder()
    {
        const QString audio = m_dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        SettingsStore settings;
        FileTranscriptionSession session(&settings, m_registry.get());
        QStringList seen;
        QVector<float> peaks;
        qint64 durationMs = 0;
        QList<qreal> progress;
        connect(&session, &FileTranscriptionSession::fileStarted, this, [&] { seen << QStringLiteral("started"); });
        connect(&session, &FileTranscriptionSession::fileDecoded, this,
                [&](int, const QVector<float> &levels, qint64 ms) {
                    seen << QStringLiteral("decoded");
                    peaks = levels;
                    durationMs = ms;
                });
        connect(&session, &FileTranscriptionSession::fileProgress, this, [&](int, qreal fraction) {
            if (seen.last() != QStringLiteral("progress")) {
                seen << QStringLiteral("progress");
            }
            progress << fraction;
        });
        connect(&session, &FileTranscriptionSession::fileRefining, this, [&] { seen << QStringLiteral("refining"); });
        connect(&session, &FileTranscriptionSession::fileFinished, this, [&] { seen << QStringLiteral("finished"); });
        connect(&session, &FileTranscriptionSession::batchFinished, this, [&] { seen << QStringLiteral("batch"); });
        TranscribeOptions options = speechOnly();
        options.refinementProviderId = QStringLiteral("openai");
        options.cleanupStrength = QStringLiteral("strong_polish");

        QVERIFY(session.start({audio}, options));
        QTRY_COMPARE_WITH_TIMEOUT(seen.size(), 6, 10000);

        QCOMPARE(seen,
                 QStringList({QStringLiteral("started"), QStringLiteral("decoded"), QStringLiteral("progress"),
                              QStringLiteral("refining"), QStringLiteral("finished"), QStringLiteral("batch")}));
        QVERIFY(qAbs(durationMs - 500) <= 1);
        // The tone's level, 8000 of 32768, across the whole length.
        QCOMPARE(peaks.size(), 240);
        for (qsizetype i = 1; i < peaks.size() - 1; ++i) {
            QVERIFY2(qAbs(peaks.at(i) - 0.244f) < 0.01f, qPrintable(QString::number(i)));
        }
        QVERIFY(std::ranges::is_sorted(progress));
        QCOMPARE(progress.last(), 1.0);
    }

    // A file's audio goes to the stream while the file still decodes, a few
    // seconds ahead of what was sent, rather than once all of it decoded.
    void aLongFileReachesTheProviderWhileItStillDecodes()
    {
        const QString audio = m_dir.filePath(QStringLiteral("talk.wav"));
        writeWav(audio, 12000);
        SettingsStore settings;
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        int sentWhileDecoding = 0;
        m_script.audioSent = [&] {
            const QList<QAudioDecoder *> decoders = session.findChildren<QAudioDecoder *>();
            if (std::ranges::any_of(decoders, &QAudioDecoder::isDecoding)) {
                ++sentWhileDecoding;
            }
        };

        QVERIFY(session.start({audio}, speechOnly()));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 20000);

        QVERIFY(qAbs(m_script.bytes - 12 * 32000) <= 8);
        // Most of the file went out while the decoder still had the rest.
        QVERIFY2(sentWhileDecoding > 60, qPrintable(QString::number(sentWhileDecoding)));
        QVERIFY(finished.first().first().value<QList<TranscribeFileResult>>().first().error.isEmpty());
    }

    // A file gone before its second pass fails there, and starts no
    // provider: the next file reaches its own whole, though it takes a while
    // to prepare.
    void aFileGoneBeforeItStreamsLeavesTheNextWhole()
    {
        const QString gone = m_dir.filePath(QStringLiteral("gone.wav"));
        const QString next = m_dir.filePath(QStringLiteral("next.wav"));
        writeWav(gone);
        writeWav(next, 12000);
        m_script.connects = true;
        SettingsStore settings;
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        bool removed = false;
        connect(&session, &FileTranscriptionSession::fileDecoded, this, [&](int index) {
            if (index == 0) {
                removed = QFile::remove(gone);
            } else {
                m_script.prepareDelayMs = 200;
            }
        });

        QVERIFY(session.start({gone, next}, speechOnly()));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 20000);
        if (!removed) {
            QSKIP("The platform kept the decoded file open");
        }

        const auto results = finished.first().first().value<QList<TranscribeFileResult>>();
        QCOMPARE(results.size(), 2);
        QVERIFY(results.at(0).error.startsWith(QStringLiteral("Could not decode the audio")));
        QVERIFY2(qAbs(results.at(1).raw.mid(6).toLongLong() - 12 * 32000) <= 8, qPrintable(results.at(1).raw));
    }

    void progressEndsOnceAllTheAudioDecodedIsSent_data()
    {
        QTest::addColumn<int>("streamedMs");
        QTest::newRow("second pass decodes less") << 500;
        QTest::newRow("second pass decodes more") << 2000;
    }

    // The bar follows what the second pass decodes when that differs from
    // what the first counted, and reaches the end once, when all of it was
    // sent.
    void progressEndsOnceAllTheAudioDecodedIsSent()
    {
        QFETCH(int, streamedMs);
        const QString audio = m_dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio, 1000);
        SettingsStore settings;
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        QList<qreal> progress;
        bool replaced = false;
        connect(&session, &FileTranscriptionSession::fileDecoded, this, [&] {
            replaced = QFile::remove(audio);
            writeWav(audio, streamedMs);
        });
        connect(&session, &FileTranscriptionSession::fileProgress, this,
                [&](int, qreal fraction) { progress << fraction; });

        QVERIFY(session.start({audio}, speechOnly()));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        if (!replaced) {
            QSKIP("The platform kept the decoded file open");
        }

        const auto results = finished.first().first().value<QList<TranscribeFileResult>>();
        QVERIFY2(qAbs(results.first().raw.mid(6).toLongLong() - streamedMs * 32) <= 8,
                 qPrintable(results.first().raw));
        QCOMPARE(progress.count(1.0), 1);
        QCOMPARE(progress.last(), 1.0);
    }

    // A refiner that cannot reach its service makes way for the saved
    // fallback, which refines the file.
    void aRefinerThatCannotReachItsServiceHandsTheFileToTheFallback()
    {
        FakeRefiner *primary = nullptr;
        m_registry->registerRefinementProvider({QStringLiteral("openai"), QStringLiteral("Fake")},
                                               [&primary](QObject *parent) {
                                                   primary = new FakeRefiner(parent);
                                                   return primary;
                                               });
        m_registry->registerRefinementProvider({QStringLiteral("local"), QStringLiteral("Local Runner")},
                                               [](QObject *parent) {
                                                   auto *fallback = new FakeRefiner(parent);
                                                   fallback->autoComplete = true;
                                                   fallback->autoCompleteText = QStringLiteral("Tidied.");
                                                   return fallback;
                                               });
        const QString audio = m_dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        SettingsStore settings;
        settings.edit([](AppSettings &draft) {
            draft.refinement.fallbackProviderIds = {QStringLiteral("local")};
            return true;
        });
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        TranscribeOptions options = speechOnly();
        options.refinementProviderId = QStringLiteral("openai");
        options.cleanupStrength = QStringLiteral("balanced");

        QVERIFY(session.start({audio}, options));
        QTRY_COMPARE_WITH_TIMEOUT(primary ? primary->refineCalls : 0, 1, 10000);
        primary->emitFailure(QStringLiteral("offline"), ProviderFailureKind::Network);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);

        const TranscribeFileResult result = finished.first().first().value<QList<TranscribeFileResult>>().first();
        QCOMPARE(result.refined, QStringLiteral("Tidied."));
        QCOMPARE(result.error, QString());
    }

    // A file no refiner could refine keeps its text as heard, and says why
    // for each refiner tried, whether it failed preparing or refining.
    void aFileNoRefinerRefinedNamesEachOneTried()
    {
        FakeRefiner *primary = nullptr;
        m_registry->registerRefinementProvider({QStringLiteral("openai"), QStringLiteral("Fake")},
                                               [&primary](QObject *parent) {
                                                   primary = new FakeRefiner(parent);
                                                   return primary;
                                               });
        m_registry->registerRefinementProvider({QStringLiteral("local"), QStringLiteral("Local Runner")},
                                               [](QObject *parent) {
                                                   auto *fallback = new FakeRefiner(parent);
                                                   fallback->prepareFailure =
                                                       ProviderFailure{ProviderFailureKind::Unavailable,
                                                                       QStringLiteral("No model is downloaded")};
                                                   return fallback;
                                               });
        const QString audio = m_dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        SettingsStore settings;
        settings.edit([](AppSettings &draft) {
            draft.refinement.fallbackProviderIds = {QStringLiteral("local")};
            return true;
        });
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        TranscribeOptions options = speechOnly();
        options.refinementProviderId = QStringLiteral("openai");
        options.cleanupStrength = QStringLiteral("balanced");

        QVERIFY(session.start({audio}, options));
        QTRY_COMPARE_WITH_TIMEOUT(primary ? primary->refineCalls : 0, 1, 10000);
        primary->emitFailure(QStringLiteral("offline"), ProviderFailureKind::Network);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);

        const TranscribeFileResult result = finished.first().first().value<QList<TranscribeFileResult>>().first();
        QCOMPARE(result.refined, QStringLiteral("heard 16000"));
        QCOMPARE(result.error,
                 QStringLiteral("Refinement failed: Fake: offline; Local Runner: No model is downloaded"));
    }

    // A batch set to None still refines, at Light, when its profile
    // translates, and says so.
    void anExplicitNoneStillTranslates()
    {
        const QString audio = m_dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        SettingsStore settings;
        AppSettings stored = settings.snapshot();
        stored.refinement.writingProfiles = {{WritingProfile::Other, QStringLiteral("balanced"), QStringLiteral("none"),
                                              QString(), QString(), QStringLiteral("Spanish")}};
        settings.applySnapshot(stored);
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        TranscribeOptions options = speechOnly();
        options.refinementProviderId = QStringLiteral("openai");
        options.cleanupStrength = QStringLiteral("none");
        options.writingProfile = WritingProfile::Other;
        QVERIFY(refinesTranscripts(options, settings.snapshot().refinement));

        QVERIFY(session.start({audio}, options));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        QCOMPARE(m_refinedWith, QStringList({QStringLiteral("light_cleanup"), QStringLiteral("none")}));
        QCOMPARE(finished.first().first().value<QList<TranscribeFileResult>>().first().refined,
                 QStringLiteral("Heard it."));
    }

    void rollsOverAStreamThatEndsMidFile()
    {
        SpeechAttemptPolicy::setStableAttemptMs(0);
        const auto restore = qScopeGuard([] { SpeechAttemptPolicy::setStableAttemptMs(10000); });
        m_script.expireFirstStream = true;
        const QString audio = m_dir.filePath(QStringLiteral("long.wav"));
        writeWav(audio);
        SettingsStore settings;
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);

        QVERIFY(session.start({audio}, speechOnly()));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);

        const auto results = finished.first().first().value<QList<TranscribeFileResult>>();
        QCOMPARE(m_script.attempts, 2);
        QVERIFY(results.first().raw.startsWith(QStringLiteral("before heard ")));
        QVERIFY(results.first().error.isEmpty());
    }

    // Transcribe asks for timings, and keeps the segments that can be cues,
    // timed from the start of the file: the second attempt's audio starts
    // where the first stream's ended.
    void timesUsableSegmentsFromTheStartOfTheFile()
    {
        SpeechAttemptPolicy::setStableAttemptMs(0);
        const auto restore = qScopeGuard([] { SpeechAttemptPolicy::setStableAttemptMs(10000); });
        m_script.expireFirstStream = true;
        m_script.segments = {{0, 200, QStringLiteral("heard")},
                             {300, 300, QStringLiteral("no length")},
                             {400, 500, QStringLiteral(" ")}};
        const QString audio = m_dir.filePath(QStringLiteral("timed.wav"));
        writeWav(audio);
        SettingsStore settings;
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);

        QVERIFY(session.start({audio}, speechOnly()));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);

        const auto results = finished.first().first().value<QList<TranscribeFileResult>>();
        QVERIFY(m_script.timedSegments);
        // The second stream starts where the first one's audio ended: one
        // chunk, which is short when the provider was ready before the file
        // had decoded that far.
        QVERIFY(m_script.firstStreamBytes > 0);
        const qint64 secondStartMs = m_script.firstStreamBytes * 1000 / 32000;
        QCOMPARE(results.first().segments,
                 QList<TranscriptSegment>({{secondStartMs, secondStartMs + 200, QStringLiteral("heard")}}));
    }

    // Every segment starts a cue; a long one splits at its sentence end, or
    // at commas packed back up to the limit, its time shared by length.
    void writesSrtCuesSplitWithinSegments()
    {
        const QList<TranscriptSegment> segments{
            {0, 1500, QStringLiteral("Hello there.")},
            {2000, 11000, QStringLiteral("The first sentence here runs for a little while, as sentences do. "
                                         "The second one is shorter.")},
            {12000, 22400, QStringLiteral("One clause that keeps going on and on, another clause that also "
                                          "keeps going, and a third clause to end it")},
        };
        QCOMPARE(subtitleFile(segments, TranscriptFormat::Srt),
                 QStringLiteral("1\n00:00:00,000 --> 00:00:01,500\nHello there.\n\n"
                                "2\n00:00:02,000 --> 00:00:08,428\n"
                                "The first sentence here runs for a little while, as sentences do.\n\n"
                                "3\n00:00:08,428 --> 00:00:11,000\nThe second one is shorter.\n\n"
                                "4\n00:00:12,000 --> 00:00:19,600\n"
                                "One clause that keeps going on and on, another clause that also keeps going,\n\n"
                                "5\n00:00:19,600 --> 00:00:22,400\nand a third clause to end it"));
    }

    // 10 nine-letter words: 8 fit in 84 characters, and the cue's share of
    // the 9.8 s is 79 of the 98 characters.
    void splitsUnpunctuatedTextBetweenWords()
    {
        const QString words = QStringList(10, QStringLiteral("abcdefghi")).join(QLatin1Char(' '));
        QCOMPARE(subtitleFile({{0, 9800, words}}, TranscriptFormat::Srt),
                 QStringLiteral("1\n00:00:00,000 --> 00:00:07,900\n"
                                "abcdefghi abcdefghi abcdefghi abcdefghi abcdefghi abcdefghi abcdefghi abcdefghi\n\n"
                                "2\n00:00:07,900 --> 00:00:09,800\nabcdefghi abcdefghi"));
    }

    // A blink-length cue is held for half a second, and a blank line inside
    // a segment would end its cue early.
    void keepsCuesReadable()
    {
        QCOMPARE(subtitleFile({{1000, 1200, QStringLiteral("Yes.")},
                               {2000, 4000, QStringLiteral("first line\n\nsecond line")}},
                              TranscriptFormat::Srt),
                 QStringLiteral("1\n00:00:01,000 --> 00:00:01,500\nYes.\n\n"
                                "2\n00:00:02,000 --> 00:00:04,000\nfirst line second line"));
    }

    void writesWebVttWithItsHeaderAndEscapes()
    {
        QCOMPARE(subtitleFile({{3723456, 3724000, QStringLiteral("a < b & c")}}, TranscriptFormat::WebVtt),
                 QStringLiteral("WEBVTT\n\n01:02:03.456 --> 01:02:04.000\na &lt; b &amp; c"));
    }

    // Refinement rewrites the words, so subtitles keep to the timed Raw
    // Transcript whichever version is shown.
    void subtitlesComeFromTheRawTranscriptAndNeedTimings()
    {
        TranscribeFileResult result;
        result.raw = QStringLiteral("heard words");
        result.refined = QStringLiteral("Refined words.");
        QVERIFY(canExportAs(result, TranscriptFormat::Text));
        QVERIFY(!canExportAs(result, TranscriptFormat::Srt));

        result.segments = {{0, 1000, QStringLiteral("heard words")}};
        QVERIFY(canExportAs(result, TranscriptFormat::WebVtt));
        QCOMPARE(exportedTranscript(result, TranscriptFormat::Srt, false),
                 QStringLiteral("1\n00:00:00,000 --> 00:00:01,000\nheard words"));
        QCOMPARE(exportedTranscript(result, TranscriptFormat::Text, false), QStringLiteral("Refined words."));

        result.refined.clear();
        QVERIFY(!canExportAs(result, TranscriptFormat::Text));
        QVERIFY(!canExportAs(result, TranscriptFormat::Srt));
    }

    void aFailedFileDoesNotStopTheBatch()
    {
        const QString broken = m_dir.filePath(QStringLiteral("broken.wav"));
        QFile file(broken);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("not audio at all");
        file.close();
        const QString audio = m_dir.filePath(QStringLiteral("good.wav"));
        writeWav(audio);
        SettingsStore settings;
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);

        QVERIFY(session.start({broken, audio}, speechOnly()));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);

        const auto results = finished.first().first().value<QList<TranscribeFileResult>>();
        QCOMPARE(results.size(), 2);
        QVERIFY(results.at(0).failed());
        QVERIFY(!results.at(0).error.isEmpty());
        QVERIFY(!results.at(1).failed());
        QCOMPARE(finished.first().at(1).toBool(), false);
    }

    // Each file walks the speech chain from the page's provider: one that
    // can't prepare, or fails before any audio went to it, hands the file to
    // the next saved fallback; the page's provider is not repeated.
    void aFileStartsOnTheNextProviderBeforeAnyAudioIsSent_data()
    {
        QTest::addColumn<bool>("failsInsideStartAttempt");
        QTest::newRow("preparation fails") << false;
        QTest::newRow("fails before audio") << true;
    }

    void aFileStartsOnTheNextProviderBeforeAnyAudioIsSent()
    {
        QFETCH(bool, failsInsideStartAttempt);
        QList<FakeSpeechTranscriber *> codex;
        m_registry->registerSpeechProvider({QStringLiteral("codex"), QStringLiteral("ChatGPT Codex")},
                                           [&codex, failsInsideStartAttempt](QObject *parent) {
                                               auto *speech = new FakeSpeechTranscriber(parent);
                                               if (failsInsideStartAttempt) {
                                                   speech->onStartAttempt = [speech] {
                                                       speech->emitFailure(QStringLiteral("refused"), false,
                                                                           SpeechPhase::Connect,
                                                                           ProviderFailureKind::Network);
                                                   };
                                               } else {
                                                   speech->prepareFailure =
                                                       ProviderFailure{ProviderFailureKind::Network,
                                                                       QStringLiteral("offline")};
                                               }
                                               codex.append(speech);
                                               return speech;
                                           });
        const QString first = m_dir.filePath(QStringLiteral("first.wav"));
        const QString second = m_dir.filePath(QStringLiteral("second.wav"));
        writeWav(first);
        writeWav(second);
        SettingsStore settings;
        settings.edit([](AppSettings &draft) {
            draft.speech.providerId = QStringLiteral("endpoint");
            draft.speech.fallbackProviderIds = {QStringLiteral("codex"), QStringLiteral("claude")};
            return true;
        });
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        TranscribeOptions options;
        options.speechProviderId = QStringLiteral("codex");

        QVERIFY(session.start({first, second}, options));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);

        const auto results = finished.first().first().value<QList<TranscribeFileResult>>();
        QCOMPARE(results.size(), 2);
        for (const TranscribeFileResult &result : results) {
            QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
            QVERIFY(result.raw.startsWith(QStringLiteral("heard ")));
        }
        QCOMPARE(codex.size(), 2);
        QCOMPARE(m_script.attempts, 2);
        QVERIFY(qAbs(m_script.bytes - 2 * 16000) <= 16);
    }

    // Once audio went to a provider the file stays with it: a failure then
    // fails the file, and no fallback hears the audio again.
    void aFileNeverChangesProviderOnceAudioWasSent()
    {
        FakeSpeechTranscriber *codex = nullptr;
        m_registry->registerSpeechProvider({QStringLiteral("codex"), QStringLiteral("ChatGPT Codex")},
                                           [&codex](QObject *parent) {
                                               codex = new FakeSpeechTranscriber(parent);
                                               codex->autoCompleteOnFinish = false;
                                               return codex;
                                           });
        const QString audio = m_dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        SettingsStore settings;
        settings.edit([](AppSettings &draft) {
            draft.speech.providerId = QStringLiteral("codex");
            draft.speech.fallbackProviderIds = {QStringLiteral("claude")};
            return true;
        });
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        TranscribeOptions options;
        options.speechProviderId = QStringLiteral("codex");

        QVERIFY(session.start({audio}, options));
        QTRY_VERIFY_WITH_TIMEOUT(codex && !codex->audioChunks.isEmpty(), 10000);
        codex->emitFailure(QStringLiteral("dropped"), false, SpeechPhase::Streaming, ProviderFailureKind::Network);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);

        const auto results = finished.first().first().value<QList<TranscribeFileResult>>();
        QCOMPARE(results.first().error, QStringLiteral("No speech service is available. ChatGPT Codex dropped."));
        QCOMPARE(m_script.attempts, 0);
    }

    // A provider that returned the file's text reached its service, even
    // without a stream to connect: a failure after it never hands the file
    // to a fallback.
    void aFileStaysWithAProviderThatReturnedItsText()
    {
        FakeSpeechTranscriber *codex = nullptr;
        m_registry->registerSpeechProvider({QStringLiteral("codex"), QStringLiteral("ChatGPT Codex")},
                                           [&codex](QObject *parent) {
                                               codex = new FakeSpeechTranscriber(parent);
                                               codex->autoCompleteOnFinish = false;
                                               return codex;
                                           });
        const QString audio = m_dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        SettingsStore settings;
        settings.edit([](AppSettings &draft) {
            draft.speech.providerId = QStringLiteral("codex");
            draft.speech.fallbackProviderIds = {QStringLiteral("claude")};
            return true;
        });
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        TranscribeOptions options;
        options.speechProviderId = QStringLiteral("codex");

        QVERIFY(session.start({audio}, options));
        QTRY_VERIFY_WITH_TIMEOUT(codex && !codex->audioChunks.isEmpty(), 10000);
        codex->emitAttemptText(QStringLiteral("the words"));
        codex->emitFailure(QStringLiteral("offline"), false, SpeechPhase::Connect, ProviderFailureKind::Network);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);

        QCOMPARE(m_script.attempts, 0);
    }

    // A provider that never connected only buffered what it was sent: the
    // next one starts the file over, from its first chunk.
    void aFileThatNeverConnectedStartsOverOnTheNextProvider()
    {
        FakeSpeechTranscriber *codex = nullptr;
        m_registry->registerSpeechProvider({QStringLiteral("codex"), QStringLiteral("ChatGPT Codex")},
                                           [&codex](QObject *parent) {
                                               codex = new FakeSpeechTranscriber(parent);
                                               codex->autoCompleteOnFinish = false;
                                               return codex;
                                           });
        const QString audio = m_dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        SettingsStore settings;
        settings.edit([](AppSettings &draft) {
            draft.speech.providerId = QStringLiteral("codex");
            draft.speech.fallbackProviderIds = {QStringLiteral("claude")};
            return true;
        });
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        TranscribeOptions options;
        options.speechProviderId = QStringLiteral("codex");

        QVERIFY(session.start({audio}, options));
        QTRY_VERIFY_WITH_TIMEOUT(codex && !codex->audioChunks.isEmpty(), 10000);
        codex->emitFailure(QStringLiteral("offline"), false, SpeechPhase::Connect, ProviderFailureKind::Network);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);

        const TranscribeFileResult result = finished.first().first().value<QList<TranscribeFileResult>>().first();
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(m_script.attempts, 1);
        QVERIFY(qAbs(m_script.bytes - 16000) <= 8);
        QCOMPARE(result.raw, QStringLiteral("heard %1").arg(m_script.bytes));
    }

    void aFallbackListensForItsOwnSpokenLanguage_data()
    {
        QTest::addColumn<QString>("chosen");
        QTest::addColumn<QString>("batchLanguage");
        QTest::addColumn<QString>("expected");
        QTest::newRow("chosen") << QStringLiteral("fr") << QString() << QStringLiteral("fr");
        QTest::newRow("chosen, batch language set") << QStringLiteral("fr") << QStringLiteral("es")
                                                    << QStringLiteral("fr");
        QTest::newRow("none chosen, batch language set") << QString() << QStringLiteral("es") << QStringLiteral("es");
    }

    // A fallback hears the file listening for its own Spoken Language, and
    // refinement is told that language. A batch's language replaces only the
    // Spoken Language, not one chosen for the fallback.
    void aFallbackListensForItsOwnSpokenLanguage()
    {
        QFETCH(QString, chosen);
        QFETCH(QString, batchLanguage);
        QFETCH(QString, expected);
        m_registry->registerSpeechProvider({QStringLiteral("codex"), QStringLiteral("ChatGPT Codex")},
                                           [](QObject *parent) {
                                               auto *speech = new FakeSpeechTranscriber(parent);
                                               speech->prepareFailure =
                                                   ProviderFailure{ProviderFailureKind::Network,
                                                                   QStringLiteral("offline")};
                                               return speech;
                                           });
        const QString audio = m_dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        SettingsStore settings;
        settings.edit([](AppSettings &draft) {
            draft.speech.providerId = QStringLiteral("codex");
            draft.speech.fallbackProviderIds = {QStringLiteral("claude")};
            return true;
        });
        if (!chosen.isEmpty()) {
            settings.edit([&](AppSettings &draft) {
                draft.speech.fallbackLanguages = {{QStringLiteral("claude"), chosen}};
                return true;
            });
        }
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        TranscribeOptions options;
        options.speechProviderId = QStringLiteral("codex");
        options.refinementProviderId = QStringLiteral("openai");
        options.cleanupStrength = QStringLiteral("light_cleanup");
        if (!batchLanguage.isEmpty()) {
            options.spokenLanguage = batchLanguage;
        }

        QVERIFY(session.start({audio}, options));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        QCOMPARE(m_script.attempts, 1);
        QCOMPARE(m_script.language, expected);
        QCOMPARE(m_refinedLanguage, expected);
    }

    void theRefinerRenewsOnlyToRefine_data()
    {
        QTest::addColumn<bool>("everySpeechProviderFails");
        QTest::newRow("a fallback prepares") << false;
        QTest::newRow("none prepares") << true;
    }

    // The refiner's sign-in renews once, when the transcript is refined,
    // never while the speech providers prepare.
    void theRefinerRenewsOnlyToRefine()
    {
        QFETCH(bool, everySpeechProviderFails);
        if (everySpeechProviderFails) {
            m_registry->registerSpeechProvider({QStringLiteral("claude"), QStringLiteral("Claude Voice")},
                                               [](QObject *parent) {
                                                   auto *speech = new FakeSpeechTranscriber(parent);
                                                   speech->prepareFailure =
                                                       ProviderFailure{ProviderFailureKind::Unavailable,
                                                                       QStringLiteral("signed out")};
                                                   return speech;
                                               });
        }
        FakeRefiner *refiner = nullptr;
        m_registry->registerRefinementProvider({QStringLiteral("openai"), QStringLiteral("Fake")},
                                               [&refiner](QObject *parent) {
                                                   refiner = new FakeRefiner(parent);
                                                   refiner->refreshRequired = true;
                                                   refiner->autoComplete = true;
                                                   return refiner;
                                               });
        m_registry->registerSpeechProvider({QStringLiteral("codex"), QStringLiteral("ChatGPT Codex")},
                                           [](QObject *parent) {
                                               auto *speech = new FakeSpeechTranscriber(parent);
                                               speech->prepareFailure =
                                                   ProviderFailure{ProviderFailureKind::Network,
                                                                   QStringLiteral("offline")};
                                               return speech;
                                           });
        const QString audio = m_dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        SettingsStore settings;
        settings.edit([](AppSettings &draft) {
            draft.speech.providerId = QStringLiteral("codex");
            draft.speech.fallbackProviderIds = {QStringLiteral("claude")};
            return true;
        });
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        TranscribeOptions options;
        options.speechProviderId = QStringLiteral("codex");
        options.refinementProviderId = QStringLiteral("openai");
        options.cleanupStrength = QStringLiteral("balanced");
        // Read while the file's refiner still exists: it goes once the file is
        // done. A file no speech provider took never makes one.
        int refreshes = -1;
        connect(&session, &FileTranscriptionSession::fileFinished, this,
                [&refiner, &refreshes] { refreshes = refiner ? refiner->refreshRuns() : 0; });

        QVERIFY(session.start({audio}, options));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        QCOMPARE(refreshes, everySpeechProviderFails ? 0 : 1);
        QCOMPARE(finished.first().first().value<QList<TranscribeFileResult>>().first().failed(),
                 everySpeechProviderFails);
    }

    // A cancel while the refiner's sign-in renews before refinement ends the
    // batch once: the renewal's answer never refines or finishes the file.
    void aCancelWhileTheRefinerRenewsEndsTheBatchOnce()
    {
        FakeRefiner *refiner = nullptr;
        m_registry->registerRefinementProvider({QStringLiteral("openai"), QStringLiteral("Fake")},
                                               [&refiner](QObject *parent) {
                                                   refiner = new FakeRefiner(parent);
                                                   refiner->refreshRequired = true;
                                                   refiner->refreshDelayMs = 200;
                                                   refiner->autoComplete = true;
                                                   return refiner;
                                               });
        const QString audio = m_dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        SettingsStore settings;
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy fileFinished(&session, &FileTranscriptionSession::fileFinished);
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        TranscribeOptions options = speechOnly();
        options.refinementProviderId = QStringLiteral("openai");
        options.cleanupStrength = QStringLiteral("balanced");

        QVERIFY(session.start({audio}, options));
        QTRY_COMPARE_WITH_TIMEOUT(refiner ? refiner->refreshRuns() : 0, 1, 10000);
        session.cancel();
        QTest::qWait(400);

        QCOMPARE(finished.count(), 1);
        QVERIFY(finished.first().at(1).toBool());
        QCOMPARE(fileFinished.count(), 0);
    }

    // A provider that ends its first stream before any audio went to it
    // turned the file away: the next one takes the file from its start.
    void aStreamEndingBeforeAnyAudioHandsTheFileOver()
    {
        // Read as the file lets the provider go: it is deleted after.
        int codexStarts = -1;
        qsizetype codexChunks = -1;
        m_registry->registerSpeechProvider({QStringLiteral("codex"), QStringLiteral("ChatGPT Codex")},
                                           [&codexStarts, &codexChunks](QObject *parent) {
                                               auto *codex = new FakeSpeechTranscriber(parent);
                                               codex->onStartAttempt = [codex] { codex->emitCompletion(); };
                                               codex->onCancelAttempt = [codex, &codexStarts, &codexChunks](quint64) {
                                                   codexStarts = codex->startCalls;
                                                   codexChunks = codex->audioChunks.size();
                                               };
                                               return codex;
                                           });
        const QString audio = m_dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        SettingsStore settings;
        settings.edit([](AppSettings &draft) {
            draft.speech.providerId = QStringLiteral("codex");
            draft.speech.fallbackProviderIds = {QStringLiteral("claude")};
            return true;
        });
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        TranscribeOptions options;
        options.speechProviderId = QStringLiteral("codex");

        QVERIFY(session.start({audio}, options));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);

        const TranscribeFileResult result = finished.first().first().value<QList<TranscribeFileResult>>().first();
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(codexStarts, 1);
        QCOMPARE(codexChunks, 0);
        QCOMPARE(m_script.attempts, 1);
        QCOMPARE(result.raw, QStringLiteral("heard %1").arg(m_script.bytes));
    }

    // The last provider failing once it started is named with the others.
    void theLastProviderFailingAfterItStartedIsNamed()
    {
        const auto failingInsideStartAttempt = [](const QString &message, ProviderFailureKind kind) {
            return [message, kind](QObject *parent) {
                auto *speech = new FakeSpeechTranscriber(parent);
                speech->onStartAttempt = [speech, message, kind] {
                    speech->emitFailure(message, false, SpeechPhase::Connect, kind);
                };
                return speech;
            };
        };
        m_registry->registerSpeechProvider({QStringLiteral("codex"), QStringLiteral("ChatGPT Codex")},
                                           failingInsideStartAttempt(QStringLiteral("offline"),
                                                                     ProviderFailureKind::Network));
        m_registry->registerSpeechProvider({QStringLiteral("endpoint"), QStringLiteral("Custom Endpoint")},
                                           failingInsideStartAttempt(QStringLiteral("502"),
                                                                     ProviderFailureKind::Server));
        const QString audio = m_dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        SettingsStore settings;
        settings.edit([](AppSettings &draft) {
            draft.speech.providerId = QStringLiteral("codex");
            draft.speech.fallbackProviderIds = {QStringLiteral("endpoint")};
            return true;
        });
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        TranscribeOptions options;
        options.speechProviderId = QStringLiteral("codex");

        QVERIFY(session.start({audio}, options));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);

        QCOMPARE(finished.first().first().value<QList<TranscribeFileResult>>().first().error,
                 QStringLiteral("No speech service is available. ChatGPT Codex couldn't be reached and Custom "
                                "Endpoint had a server error."));
    }

    // What the microphone hears while a provider prepares waits for it. One
    // that never connected hands all of it, and what came meanwhile, to the
    // next; once the microphone stops, nothing more is taken.
    void listeningSendsEverythingHeardToTheProviderThatConnects()
    {
        FakeSpeechTranscriber *codex = nullptr;
        m_registry->registerSpeechProvider({QStringLiteral("codex"), QStringLiteral("ChatGPT Codex")},
                                           [&codex](QObject *parent) {
                                               codex = new FakeSpeechTranscriber(parent);
                                               codex->autoCompleteOnFinish = false;
                                               return codex;
                                           });
        SettingsStore settings;
        settings.edit([](AppSettings &draft) {
            draft.speech.providerId = QStringLiteral("codex");
            draft.speech.fallbackProviderIds = {QStringLiteral("claude")};
            return true;
        });
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        FakeAudioInput microphone;
        microphone.onStart = [&microphone] { microphone.pushAudio(microphoneChunk(8000)); };
        TranscribeOptions options;
        options.speechProviderId = QStringLiteral("codex");

        QVERIFY(session.startListening(&microphone, options));
        QVERIFY(microphone.active);
        // The provider starts once the worker reports, never inside the call.
        QVERIFY(codex);
        QCOMPARE(codex->startCalls, 0);
        QTRY_VERIFY_WITH_TIMEOUT(!codex->audioChunks.isEmpty(), 10000);
        microphone.pushAudio(microphoneChunk(8000));
        codex->emitFailure(QStringLiteral("offline"), false, SpeechPhase::Connect, ProviderFailureKind::Network);
        microphone.pushAudio(microphoneChunk(8000));
        QTRY_COMPARE_WITH_TIMEOUT(m_script.bytes, 9600, 10000);
        microphone.pushAudio(microphoneChunk(0));
        session.finishListening();
        QVERIFY(!microphone.active);
        microphone.pushAudio(microphoneChunk(8000));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);

        const TranscribeFileResult result = finished.first().first().value<QList<TranscribeFileResult>>().first();
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(result.path.isEmpty());
        QVERIFY(result.savedPath.isEmpty());
        QCOMPARE(m_script.attempts, 1);
        QCOMPARE(result.raw, QStringLiteral("heard 12800"));
    }

    // Stopping, as Enter and Ctrl-C do, keeps what was said and prints it.
    void listenPrintsWhatWasSaidWhenStopped()
    {
        SettingsStore settings;
        FakeAudioInput microphone;
        bool stop = false;
        microphone.onStart = [&] {
            microphone.pushAudio(microphoneChunk(8000));
            QTimer::singleShot(200, &microphone, [&] {
                microphone.pushAudio(microphoneChunk(8000));
                stop = true;
            });
        };
        HeadlessTranscribeOptions options;
        options.speechProviderId = QStringLiteral("claude");
        options.refinementProviderId = QStringLiteral("none");
        std::ostringstream out;
        std::ostringstream err;

        QCOMPARE(runHeadlessListen(options, std::nullopt, &microphone, [&] { return stop; }, true, &settings,
                                   m_registry.get(), out, err),
                 0);
        QCOMPARE(QString::fromStdString(out.str()), QStringLiteral("heard 6400\n"));
        QVERIFY(!microphone.active);
        QVERIFY(QString::fromStdString(err.str()).contains(QStringLiteral("Press Enter or Ctrl-C to stop.")));

        options.refinementProviderId = QStringLiteral("openai");
        options.cleanupStrength = QStringLiteral("balanced");
        options.json = true;
        stop = false;
        out.str({});
        QCOMPARE(runHeadlessListen(options, std::nullopt, &microphone, [&] { return stop; }, true, &settings,
                                   m_registry.get(), out, err),
                 0);
        QCOMPARE(QJsonDocument::fromJson(QByteArray::fromStdString(out.str())).object(),
                 QJsonObject({{QStringLiteral("ok"), true}, {QStringLiteral("text"), QStringLiteral("Heard it.")}}));
    }

    // --until-silence ends the recording once speech has started and gone
    // quiet, and not during the silence before it.
    void listenStopsAfterSilenceOnceSpeechStarted()
    {
        SettingsStore settings;
        FakeAudioInput microphone;
        QElapsedTimer sinceSpeech;
        microphone.onStart = [&microphone, &sinceSpeech] {
            for (int i = 0; i < 5; ++i) {
                QTimer::singleShot(i * 100, &microphone, [&microphone] { microphone.pushAudio(microphoneChunk(0)); });
            }
            QTimer::singleShot(500, &microphone, [&microphone, &sinceSpeech] {
                microphone.pushAudio(microphoneChunk(8000));
                sinceSpeech.start();
            });
        };
        HeadlessTranscribeOptions options;
        options.speechProviderId = QStringLiteral("claude");
        options.refinementProviderId = QStringLiteral("none");
        std::ostringstream out;
        std::ostringstream err;
        // A run the 0.3 s pause does not end stops here instead, which fails
        // below: well past the pause after speech, or 10 s in all.
        QElapsedTimer clock;
        clock.start();
        bool stoppedByTest = false;
        const auto stop = [&] {
            stoppedByTest = (sinceSpeech.isValid() && sinceSpeech.elapsed() > 1500) || clock.elapsed() > 10000;
            return stoppedByTest;
        };

        QCOMPARE(runHeadlessListen(options, 300, &microphone, stop, false, &settings, m_registry.get(), out, err), 0);
        QVERIFY(!stoppedByTest);
        QCOMPARE(QString::fromStdString(out.str()), QStringLiteral("heard 19200\n"));
        // Enter is not read here, so the hint leaves it out.
        QVERIFY(QString::fromStdString(err.str()).contains(QStringLiteral("Press Ctrl-C to stop, or pause for 0.3 s.")));
    }

    void listenFailsWithoutAMicrophoneOrAKnownProvider()
    {
        SettingsStore settings;
        FakeAudioInput microphone;
        microphone.startResult = false;
        microphone.startError = QStringLiteral("No microphone was found.");
        HeadlessTranscribeOptions options;
        options.speechProviderId = QStringLiteral("claude");
        options.refinementProviderId = QStringLiteral("none");
        options.json = true;
        std::ostringstream out;
        std::ostringstream err;
        const auto run = [&] {
            return runHeadlessListen(options, std::nullopt, &microphone, [] { return true; }, false, &settings,
                                     m_registry.get(), out, err);
        };

        QCOMPARE(run(), 1);
        QCOMPARE(QJsonDocument::fromJson(QByteArray::fromStdString(out.str())).object(),
                 QJsonObject({{QStringLiteral("ok"), false},
                              {QStringLiteral("text"), QString()},
                              {QStringLiteral("error"), QStringLiteral("No microphone was found.")}}));
        QVERIFY(QString::fromStdString(err.str()).contains(QStringLiteral("No microphone was found.")));

        // Refused microphone access, which macOS asks about, fails the same way.
        out.str({});
        QCOMPARE(runHeadlessListen(options, std::nullopt, nullptr, [] { return true; }, false, &settings,
                                   m_registry.get(), out, err),
                 1);
        const QJsonObject refused = QJsonDocument::fromJson(QByteArray::fromStdString(out.str())).object();
        QCOMPARE(refused.value(QStringLiteral("ok")), QJsonValue(false));
        QVERIFY(refused.value(QStringLiteral("error")).toString().startsWith(
            QStringLiteral("Microphone access is off")));

        microphone.startResult = true;
        options.speechProviderId = QStringLiteral("nope");
        out.str({});
        QCOMPARE(run(), 2);
        QVERIFY(!microphone.started);
        QCOMPARE(QJsonDocument::fromJson(QByteArray::fromStdString(out.str())).object().value(QStringLiteral("ok")),
                 QJsonValue(false));
    }

    // One line per utterance, timed from the start and flushed at once; a file
    // that exists is never written to, the next free name is.
    void aRecordingTranscriptAppendsTimedLinesBesideExistingFiles()
    {
        QCOMPARE(defaultRecordingPath(QStringLiteral("/data"), QDateTime(QDate(2026, 10, 7), QTime(9, 5))),
                 QStringLiteral("/data/recordings/2026-10-07-0905.md"));
        const QString path = m_dir.filePath(QStringLiteral("calls/call.md"));
        RecordingTranscript first;
        QString error;
        QVERIFY2(first.create(path, &error), qPrintable(error));
        QCOMPARE(first.path(), path);
        QVERIFY(first.append(3723456, QStringLiteral("me"), QStringLiteral(" Can you\nlook at it? "), &error));
        // Read while the file is open, as tail -f does.
        QCOMPARE(fileBytes(path), QByteArray("[01:02:03] me: Can you look at it?\n"));
        QVERIFY(first.append(3724000, QStringLiteral("me"), QStringLiteral("Yes."), &error));
        first.close();

        RecordingTranscript second;
        QVERIFY(second.create(path, &error));
        QCOMPARE(second.path(), m_dir.filePath(QStringLiteral("calls/call-2.md")));
        RecordingTranscript third;
        QVERIFY(third.create(path, &error));
        QCOMPARE(third.path(), m_dir.filePath(QStringLiteral("calls/call-3.md")));
        QCOMPARE(fileBytes(path), QByteArray("[01:02:03] me: Can you look at it?\n[01:02:04] me: Yes.\n"));
    }

    // Each final is written as it arrives, and so is the partial a stream
    // leaves when the provider ends it; the next stream carries on, and
    // stopping writes the last one.
    void aRecordingAppendsFinalsInOrderThroughARollover()
    {
        SpeechAttemptPolicy::setStableAttemptMs(0);
        const auto restore = qScopeGuard([] { SpeechAttemptPolicy::setStableAttemptMs(10000); });
        registerStreamingCodex();
        SettingsStore settings;
        settings.edit([](AppSettings &draft) { draft.speech.providerId = QStringLiteral("codex"); return true; });
        QPointer<FakeAudioInput> microphone;
        bool microphoneStopped = false;
        RecordingSession recording(&settings, m_registry.get(), [&](QObject *parent) {
            microphone = new FakeAudioInput(parent);
            microphone->onStop = [&microphoneStopped] { microphoneStopped = true; };
            return microphone.data();
        });
        QSignalSpy stopped(&recording, &RecordingSession::stopped);

        QString error;
        const QString path = startRecording(recording, QString(), &error);
        QVERIFY2(!path.isEmpty(), qPrintable(error));
        QVERIFY(path.startsWith(m_dir.filePath(QStringLiteral("recordings/"))));
        QVERIFY(recording.isRecording());
        microphone->pushAudio(microphoneChunk(8000));
        QTRY_VERIFY_WITH_TIMEOUT(!m_codex->audioChunks.isEmpty(), 10000);
        m_codex->emitPartialText(QStringLiteral("Can you"));
        m_codex->emitFinalText(QStringLiteral("Can you look at the retry logic?"));
        QCOMPARE(recordedTexts(path), QStringList{QStringLiteral("Can you look at the retry logic?")});

        m_codex->emitPartialText(QStringLiteral("I'll check"));
        m_codex->emitCompletion();
        QCOMPARE(m_codex->startCalls, 2);
        microphone->pushAudio(microphoneChunk(8000));
        m_codex->emitFinalText(QStringLiteral("it today."));
        m_codex->emitPartialText(QStringLiteral("Bye"));
        // The provider finishes the last utterance once the input ends.
        m_codex->autoCompleteOnFinish = false;
        recording.stop();
        QVERIFY(microphoneStopped);
        QTRY_COMPARE_WITH_TIMEOUT(m_codex->stopCalls, 1, 10000);
        QCOMPARE(stopped.count(), 0);
        m_codex->emitFinalText(QStringLiteral("Bye."));
        m_codex->emitCompletion();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 10000);

        QVERIFY(!recording.isRecording());
        QCOMPARE(stopped.first().first().value<RecordingStatus>().path, path);
        QCOMPARE(recordedTexts(path), QStringList({QStringLiteral("Can you look at the retry logic?"),
                                                   QStringLiteral("I'll check"), QStringLiteral("it today."),
                                                   QStringLiteral("Bye.")}));
    }

    // record start --vocab-file's terms reach the speech provider ahead of the
    // saved ones, for this recording only.
    void aRecordingAddsItsTermsToTheVocabulary()
    {
        registerStreamingCodex();
        SettingsStore settings;
        settings.edit([](AppSettings &draft) {
            draft.speech.providerId = QStringLiteral("codex");
            draft.vocabulary = {{QStringLiteral("Speecher")}};
            return true;
        });
        const QList<VocabularyEntry> saved = settings.dictationSnapshot().vocabulary;
        RecordingSession recording(&settings, m_registry.get(), [](QObject *parent) {
            return new FakeAudioInput(parent);
        });
        QString error;
        QVERIFY2(!startRecording(recording, QString(), &error, {QStringLiteral("readSharedChoice")}).isEmpty(),
                 qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(m_codex && m_codex->startCalls == 1, 10000);
        QCOMPARE(m_codex->lastVocabulary, QStringList({QStringLiteral("readSharedChoice"), QStringLiteral("Speecher")}));
        QCOMPARE(settings.dictationSnapshot().vocabulary, saved);
    }

    // The Other profile's speech model applies where that profile keeps the
    // recording's provider, as in a Transcribe batch.
    void aRecordingUsesTheOtherProfilesModel()
    {
        QPointer<FakeSpeechTranscriber> local;
        m_registry->registerSpeechProvider({QStringLiteral("local"), QStringLiteral("Local Model")},
                                           [&local](QObject *parent) {
                                               local = new FakeSpeechTranscriber(parent);
                                               local->streamsFinals = true;
                                               local->onStartAttempt = [&local] { local->emitConnected(); };
                                               return local.data();
                                           });
        SettingsStore settings;
        AppSettings draft = settings.snapshot();
        draft.speech.providerId = QStringLiteral("local");
        for (WritingProfileSettings &profile : draft.refinement.writingProfiles) {
            if (profile.profile == WritingProfile::Other) {
                profile.speechProvider = QStringLiteral("local");
                profile.speechModel = QStringLiteral("moonshine-small");
            }
        }
        settings.applySnapshot(draft);
        RecordingSession recording(&settings, m_registry.get(), [](QObject *parent) {
            return new FakeAudioInput(parent);
        });
        QString error;
        QVERIFY2(!startRecording(recording, QString(), &error).isEmpty(), qPrintable(error));
        QCOMPARE(local->startCalls, 1);
        QCOMPARE(local->lastLocalModel, QStringLiteral("moonshine-small"));
    }

    // A provider that finalizes only when asked, as Codex does, still writes
    // each utterance while the recording runs: a pause after speech ends it,
    // once, and quiet audio does not start another.
    void aRecordingEndsAnUtteranceAtAPause()
    {
        registerStreamingCodex();
        SettingsStore settings;
        settings.edit([](AppSettings &draft) { draft.speech.providerId = QStringLiteral("codex"); return true; });
        QPointer<FakeAudioInput> microphone;
        RecordingSession recording(&settings, m_registry.get(), [&](QObject *parent) {
            microphone = new FakeAudioInput(parent);
            return microphone.data();
        });
        QString error;
        const QString path = startRecording(recording, QString(), &error);
        QVERIFY2(!path.isEmpty(), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(m_codex && m_codex->startCalls == 1, 10000);
        const QStringList utterances{QStringLiteral("Can you look at the retry logic?"),
                                     QStringLiteral("It drops the last chunk.")};
        int ends = 0;
        m_codex->onEndUtterance = [&] { m_codex->emitFinalText(utterances.value(ends++)); };

        microphone->pushAudio(microphoneChunk(8000));
        microphone->pushAudio(microphoneChunk(0));
        QCOMPARE(ends, 0);
        QTRY_COMPARE_WITH_TIMEOUT(recordedTexts(path), utterances.mid(0, 1), 2000);
        for (int i = 0; i < 10; ++i) {
            microphone->pushAudio(microphoneChunk(0));
            QTest::qWait(100);
        }
        QCOMPARE(ends, 1);
        microphone->pushAudio(microphoneChunk(8000));
        QTRY_COMPARE_WITH_TIMEOUT(recordedTexts(path), utterances, 2000);
        QCOMPARE(ends, 2);
    }

    // Speech with no pause is ended at 25 s of audio, under Codex's 30 s
    // limit, and what follows is the next utterance.
    void aRecordingEndsALongUtteranceAt25Seconds()
    {
        registerStreamingCodex();
        SettingsStore settings;
        settings.edit([](AppSettings &draft) { draft.speech.providerId = QStringLiteral("codex"); return true; });
        QPointer<FakeAudioInput> microphone;
        RecordingSession recording(&settings, m_registry.get(), [&](QObject *parent) {
            microphone = new FakeAudioInput(parent);
            return microphone.data();
        });
        QString error;
        const QString path = startRecording(recording, QString(), &error);
        QVERIFY2(!path.isEmpty(), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(m_codex && m_codex->startCalls == 1, 10000);
        // Seconds of audio the provider had when asked each time.
        QList<qsizetype> heardAtEnds;
        m_codex->onEndUtterance = [&] {
            heardAtEnds << byteCount(m_codex->audioChunks) / (16000 * 2);
            m_codex->emitFinalText(QStringLiteral("Part %1.").arg(heardAtEnds.size()));
        };

        // 30 s, in the microphone's 100 ms chunks.
        for (int i = 0; i < 300; ++i) {
            microphone->pushAudio(microphoneChunk(8000));
        }
        // The recording works through all 30 s as fast as the CPU allows: about
        // 10 s on a macOS runner shared with the other suites.
        QTRY_COMPARE_WITH_TIMEOUT(recordedTexts(path), QStringList({QStringLiteral("Part 1."), QStringLiteral("Part 2.")}),
                                  30000);
        QCOMPARE(heardAtEnds, QList<qsizetype>({25, 30}));
    }

    // An utterance's end reaches the provider right after its last audio,
    // even when the audio waiting to be sent does not split there.
    void aRecordingEndsAnUtteranceRightAfterItsAudio()
    {
        registerStreamingCodex();
        SettingsStore settings;
        settings.edit([](AppSettings &draft) { draft.speech.providerId = QStringLiteral("codex"); return true; });
        QPointer<FakeAudioInput> microphone;
        RecordingSession recording(&settings, m_registry.get(), [&](QObject *parent) {
            microphone = new FakeAudioInput(parent);
            return microphone.data();
        });
        QString error;
        QVERIFY2(!startRecording(recording, QString(), &error).isEmpty(), qPrintable(error));
        // Bytes the provider had when asked each time.
        QList<qsizetype> heardAtEnds;
        m_codex->onEndUtterance = [&] { heardAtEnds << byteCount(m_codex->audioChunks); };

        // 3,000-byte pieces: the 25 s limit ends the utterance after the
        // 267th, at 801,000 bytes, part way through a 3,200-byte send; the
        // pause after the last ends the next.
        const QByteArray piece = microphoneChunk(8000).left(3000);
        for (int i = 0; i < 320; ++i) {
            microphone->pushAudio(piece);
        }
        // As fast as the CPU allows: about 10 s on a busy macOS runner.
        QTRY_COMPARE_WITH_TIMEOUT(heardAtEnds, QList<qsizetype>({801000, 320 * 3000}), 30000);
    }

    // The Custom Endpoint records too: the pause after each utterance, or
    // the stop, uploads it and its text is a line. One the server fails is
    // missing, status says so while the recording goes on, and the next
    // still goes, with the text before as its prompt.
    void aRecordingWithTheEndpointUploadsEachUtteranceAndReportsOneThatFailed()
    {
        FakeServer server;
        for (const QByteArray &response :
             {httpResponse("200 OK", "application/json", "{\"text\":\"Can you look at the retry logic?\"}"),
              httpResponse("500 Internal Server Error", "application/json", "{\"error\":{\"message\":\"overloaded\"}}"),
              httpResponse("200 OK", "application/json", "{\"text\":\"It drops the last chunk.\"}")}) {
            server.route("POST /v1/audio/transcriptions", response);
        }
        SettingsStore settings;
        useEndpoint(settings, server);
        QPointer<FakeAudioInput> microphone;
        RecordingSession recording(&settings, m_registry.get(), [&](QObject *parent) {
            microphone = new FakeAudioInput(parent);
            return microphone.data();
        });
        QSignalSpy stopped(&recording, &RecordingSession::stopped);
        QString error;
        const QString path = startRecording(recording, QString(), &error);
        QVERIFY2(!path.isEmpty(), qPrintable(error));
        QSignalSpy problems(&recording, &RecordingSession::problemChanged);

        microphone->pushAudio(microphoneChunk(8000));
        QTRY_COMPARE_WITH_TIMEOUT(recordedTexts(path), QStringList{QStringLiteral("Can you look at the retry logic?")},
                                  3000);
        microphone->pushAudio(microphoneChunk(8000));
        QTRY_VERIFY_WITH_TIMEOUT(!recording.status().streams.first().problem.isEmpty(), 3000);
        QCOMPARE(recording.status().streams.first().problem,
                 QStringLiteral("An utterance could not be transcribed: Speech endpoint failed: overloaded"));
        QCOMPARE(problems.count(), 1);
        QCOMPARE(recording.status().streams.first().state, RecordingStreamStatus::State::Recording);

        microphone->pushAudio(microphoneChunk(8000));
        recording.stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 3000);
        QCOMPARE(recordedTexts(path), QStringList({QStringLiteral("Can you look at the retry logic?"),
                                                   QStringLiteral("It drops the last chunk.")}));
        QCOMPARE(server.requests.size(), 3);
        QVERIFY(server.requests.at(2).contains("Can you look at the retry logic?\r\n"));
    }

    // The quiet before an utterance is not uploaded, but for a 300 ms
    // lead-in.
    void aRecordingWithTheEndpointUploadsOnlyTheSpeechAfterALongQuiet()
    {
        FakeServer server;
        server.route("POST /v1/audio/transcriptions", httpResponse("200 OK", "application/json", "{\"text\":\"Yes.\"}"));
        SettingsStore settings;
        useEndpoint(settings, server);
        QPointer<FakeAudioInput> microphone;
        RecordingSession recording(&settings, m_registry.get(), [&](QObject *parent) {
            microphone = new FakeAudioInput(parent);
            return microphone.data();
        });
        QString error;
        const QString path = startRecording(recording, QString(), &error);
        QVERIFY2(!path.isEmpty(), qPrintable(error));

        // Ten seconds of quiet, then "yes".
        for (int i = 0; i < 100; ++i) {
            microphone->pushAudio(microphoneChunk(0));
        }
        microphone->pushAudio(microphoneChunk(8000));
        // As fast as the CPU allows: a few seconds on a busy macOS runner.
        QTRY_COMPARE_WITH_TIMEOUT(recordedTexts(path), QStringList{QStringLiteral("Yes.")}, 20000);
        QCOMPARE(server.requests.size(), 1);
        QCOMPARE(uploadedAudioBytes(server.requests.first()), 9600 + microphoneChunk(8000).size());
    }

    // A recording that hears no speech uploads nothing and stops with
    // nothing missed.
    void aSilentRecordingWithTheEndpointUploadsNothing()
    {
        FakeServer server;
        SettingsStore settings;
        useEndpoint(settings, server);
        QPointer<FakeAudioInput> microphone;
        RecordingSession recording(&settings, m_registry.get(), [&](QObject *parent) {
            microphone = new FakeAudioInput(parent);
            return microphone.data();
        });
        QSignalSpy stopped(&recording, &RecordingSession::stopped);
        QString error;
        QVERIFY2(!startRecording(recording, QString(), &error).isEmpty(), qPrintable(error));

        for (int i = 0; i < 20; ++i) {
            microphone->pushAudio(microphoneChunk(0));
        }
        recording.stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 3000);
        const RecordingStatus status = stopped.first().first().value<RecordingStatus>();
        QCOMPARE(recordingStreamProblemText(status.streams.first()), QString());
        QCOMPARE(recordingWriteProblemText(status), QString());
        QVERIFY(server.requests.isEmpty());
    }

    void aRecordingStopsTheStreamOfAnEndpointThatFailsEveryUtterance_data()
    {
        QTest::addColumn<QByteArray>("response");
        QTest::newRow("a path the server does not have") << QByteArray();
        QTest::newRow("a refused key")
            << httpResponse("401 Unauthorized", "application/json", "{\"error\":{\"message\":\"Incorrect API key\"}}");
        QTest::newRow("a spent quota")
            << httpResponse("429 Too Many Requests", "application/json",
                            "{\"error\":{\"message\":\"You exceeded your current quota\",\"code\":\"insufficient_quota\"}}");
    }

    // An endpoint that fails in a way every utterance would stops the stream,
    // and status says why until the stop, which says it too.
    void aRecordingStopsTheStreamOfAnEndpointThatFailsEveryUtterance()
    {
        QFETCH(QByteArray, response);
        FakeServer server;
        if (!response.isEmpty()) {
            server.route("POST /v1/audio/transcriptions", response);
        }
        SettingsStore settings;
        useEndpoint(settings, server);
        QPointer<FakeAudioInput> microphone;
        RecordingSession recording(&settings, m_registry.get(), [&](QObject *parent) {
            microphone = new FakeAudioInput(parent);
            return microphone.data();
        });
        QSignalSpy stopped(&recording, &RecordingSession::stopped);
        QString error;
        QVERIFY2(!startRecording(recording, QString(), &error).isEmpty(), qPrintable(error));

        microphone->pushAudio(microphoneChunk(8000));
        QTRY_COMPARE_WITH_TIMEOUT(recording.status().streams.first().state, RecordingStreamStatus::State::Stopped,
                                  3000);
        QVERIFY2(recording.status().streams.first().problem.startsWith(QStringLiteral("Speech endpoint failed: ")),
                 qPrintable(recording.status().streams.first().problem));
        QVERIFY(recording.isRecording());
        microphone->pushAudio(microphoneChunk(8000));
        QTest::qWait(1200);
        QCOMPARE(server.requests.size(), 1);
        QCOMPARE(recording.status().streams.first().state, RecordingStreamStatus::State::Stopped);

        recording.stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 3000);
        const RecordingStreamStatus stream = stopped.first().first().value<RecordingStatus>().streams.first();
        QVERIFY2(stream.problem.startsWith(QStringLiteral("Speech endpoint failed: ")), qPrintable(stream.problem));
        QVERIFY(!recordingStreamProblemText(stream).isEmpty());
    }

    // A stop waits as long as the provider may take to answer each utterance
    // left, from the last answer, one with no words too, and then gives up.
    void aRecordingStopWaitsAsLongAsItsProviderTakesForEachUtterance()
    {
        registerStreamingCodex();
        SettingsStore settings;
        settings.edit([](AppSettings &draft) { draft.speech.providerId = QStringLiteral("codex"); return true; });
        RecordingSession recording(&settings, m_registry.get(), [](QObject *parent) {
            return new FakeAudioInput(parent);
        });
        QSignalSpy stopped(&recording, &RecordingSession::stopped);
        QString error;
        const QString path = startRecording(recording, QString(), &error);
        QVERIFY2(!path.isEmpty(), qPrintable(error));
        m_codex->utteranceAnswerTimeout = 1200;
        m_codex->autoCompleteOnFinish = false;
        recording.stop();
        QTimer::singleShot(800, m_codex, [codex = m_codex] { codex->emitFinalText(QString()); });
        QTimer::singleShot(1600, m_codex, [codex = m_codex] {
            codex->emitFinalText(QStringLiteral("Yes."));
            codex->emitCompletion();
        });
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 5000);
        QCOMPARE(recordingStreamProblemText(stopped.first().first().value<RecordingStatus>().streams.first()),
                 QString());
        QCOMPARE(recordedTexts(path), QStringList{QStringLiteral("Yes.")});

        QVERIFY2(!startRecording(recording, QString(), &error).isEmpty(), qPrintable(error));
        m_codex->utteranceAnswerTimeout = 1200;
        m_codex->autoCompleteOnFinish = false;
        recording.stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 2, 3000);
        QCOMPARE(stopped.last().first().value<RecordingStatus>().streams.first().problem,
                 recordingStopTimedOutText());
    }

    // A stop ends the utterance being spoken once the microphone has
    // delivered its post-roll, so the upload carries it.
    void aRecordingStopUploadsTheMicrophonesPostRoll()
    {
        FakeServer server;
        server.route("POST /v1/audio/transcriptions", httpResponse("200 OK", "application/json", "{\"text\":\"Yes.\"}"));
        SettingsStore settings;
        useEndpoint(settings, server);
        QPointer<FakeAudioInput> microphone;
        RecordingSession recording(&settings, m_registry.get(), [&](QObject *parent) {
            microphone = new FakeAudioInput(parent);
            return microphone.data();
        });
        QSignalSpy stopped(&recording, &RecordingSession::stopped);
        QString error;
        const QString path = startRecording(recording, QString(), &error);
        QVERIFY2(!path.isEmpty(), qPrintable(error));

        const QByteArray postRoll = microphoneChunk(1234);
        microphone->pushAudio(microphoneChunk(8000));
        microphone->onStop = [&] { microphone->pushAudio(postRoll); };
        recording.stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 3000);
        QCOMPARE(recordedTexts(path), QStringList{QStringLiteral("Yes.")});
        QCOMPARE(server.requests.size(), 1);
        QVERIFY(server.requests.first().contains(microphoneChunk(8000) + postRoll));
    }

    // A provider that transcribes only once the audio ends cannot record yet,
    // and a microphone that cannot start leaves no file. A stream that fails
    // part way in a way a reconnect cannot mend stops, with the partial it
    // left written; the recording says why until it is stopped, and the
    // stopped recording still says it.
    void aRecordingRefusesWhatCannotRecordAndReportsAStoppedStream()
    {
        SettingsStore settings;
        settings.edit([](AppSettings &draft) { draft.speech.providerId = QStringLiteral("claude"); return true; });
        bool microphoneStarts = true;
        RecordingSession recording(&settings, m_registry.get(), [&microphoneStarts](QObject *parent) {
            auto *microphone = new FakeAudioInput(parent);
            microphone->startResult = microphoneStarts;
            microphone->startError = QStringLiteral("No microphone was found.");
            return microphone;
        });
        QSignalSpy stopped(&recording, &RecordingSession::stopped);
        QString error;
        const QString refused = m_dir.filePath(QStringLiteral("refused.md"));
        QVERIFY(startRecording(recording, refused, &error).isEmpty());
        QCOMPARE(error, QStringLiteral("Recording needs Claude, ChatGPT or the Custom Endpoint. Scripted "
                                       "can't record yet."));
        QVERIFY(!QFileInfo::exists(refused));

        registerStreamingCodex();
        settings.edit([](AppSettings &draft) { draft.speech.providerId = QStringLiteral("codex"); return true; });
        microphoneStarts = false;
        const QString deaf = m_dir.filePath(QStringLiteral("deaf.md"));
        QVERIFY(startRecording(recording, deaf, &error).isEmpty());
        QCOMPARE(error, QStringLiteral("No microphone was found."));
        QVERIFY(!QFileInfo::exists(deaf));
        QVERIFY(!recording.isRecording());

        microphoneStarts = true;
        const QString path = startRecording(recording, m_dir.filePath(QStringLiteral("call.md")), &error);
        QCOMPARE(path, m_dir.filePath(QStringLiteral("call.md")));
        m_codex->emitFinalText(QStringLiteral("Said before."));
        m_codex->emitPartialText(QStringLiteral("And then"));
        m_codex->emitFailure(QStringLiteral("Unsupported audio"), false, SpeechPhase::Streaming,
                             ProviderFailureKind::Other);
        const RecordingStatus status = recording.status();
        QVERIFY(status.recording);
        QCOMPARE(status.path, path);
        QCOMPARE(status.streams.size(), 1);
        QCOMPARE(status.streams.first().speaker, QStringLiteral("me"));
        QCOMPARE(status.streams.first().state, RecordingStreamStatus::State::Stopped);
        QCOMPARE(status.streams.first().problem, QStringLiteral("Unsupported audio"));

        recording.stop();
        QCOMPARE(stopped.count(), 1);
        QVERIFY(!recording.isRecording());
        QCOMPARE(stopped.first().first().value<RecordingStatus>().streams.first().problem, QStringLiteral("Unsupported audio"));
        QCOMPARE(recordedTexts(path), QStringList({QStringLiteral("Said before."), QStringLiteral("And then")}));
    }

    // The start answers only once a stream is up: when no provider connects
    // (signed out, offline, a second session refused), it fails with the
    // reason and leaves no file.
    void aRecordingThatNeverConnectsFailsToStart()
    {
        registerStreamingCodex(false);
        SettingsStore settings;
        settings.edit([](AppSettings &draft) { draft.speech.providerId = QStringLiteral("codex"); return true; });
        RecordingSession recording(&settings, m_registry.get(), [](QObject *parent) {
            return new FakeAudioInput(parent);
        });
        const QString path = m_dir.filePath(QStringLiteral("never.md"));
        std::optional<QString> outcome;
        recording.start(path, {}, m_dir.path(), false, [&outcome](const QString &error) { outcome = error; });
        QTRY_VERIFY_WITH_TIMEOUT(m_codex && m_codex->startCalls == 1, 10000);
        QVERIFY(QFileInfo::exists(path));
        QVERIFY(!outcome);
        QVERIFY(!recording.isRecording());

        m_codex->emitFailure(QStringLiteral("Too many sessions"), true, SpeechPhase::Connect,
                             ProviderFailureKind::Network);
        QCOMPARE(outcome, std::optional<QString>(QStringLiteral("Too many sessions")));
        QVERIFY(!QFileInfo::exists(path));
        QVERIFY(!recording.isRecording());
    }

    // A stream that drops keeps reconnecting, after growing pauses, for as
    // long as the microphone runs, even through failed connects. status says
    // it is reconnecting, the partial it left is written, and the words
    // spoken meanwhile go to the stream that returns.
    void aRecordingReconnectsADroppedStreamAndSendsWhatWaited()
    {
        registerStreamingCodex();
        SettingsStore settings;
        settings.edit([](AppSettings &draft) { draft.speech.providerId = QStringLiteral("codex"); return true; });
        QPointer<FakeAudioInput> microphone;
        RecordingSession recording(&settings, m_registry.get(), [&](QObject *parent) {
            microphone = new FakeAudioInput(parent);
            return microphone.data();
        });
        QString error;
        const QString path = startRecording(recording, QString(), &error);
        QVERIFY2(!path.isEmpty(), qPrintable(error));
        m_codex->emitPartialText(QStringLiteral("Before the drop"));

        // A connect fails while the network is still down; the next gets
        // through.
        int connects = 0;
        m_codex->onStartAttempt = [this, &connects] {
            if (++connects < 2) {
                m_codex->emitFailure(QStringLiteral("Host unreachable"), true, SpeechPhase::Connect,
                                     ProviderFailureKind::Network);
                return;
            }
            m_codex->emitConnected();
        };
        m_codex->emitFailure(QStringLiteral("Connection reset"), true, SpeechPhase::Streaming,
                             ProviderFailureKind::Network);
        QCOMPARE(recording.status().streams.first().state, RecordingStreamStatus::State::Reconnecting);
        QCOMPARE(recording.status().streams.first().problem, QStringLiteral("Connection reset"));
        QCOMPARE(recordedTexts(path), QStringList{QStringLiteral("Before the drop")});
        m_codex->audioChunks.clear();
        const QByteArray spoken = microphoneChunk(1234);
        microphone->pushAudio(spoken);

        QTRY_COMPARE_WITH_TIMEOUT(recording.status().streams.first().state, RecordingStreamStatus::State::Recording,
                                  10000);
        QCOMPARE(connects, 2);
        QVERIFY(recording.status().streams.first().problem.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(m_codex->audioChunks.contains(spoken), 2000);
    }

#ifdef Q_OS_UNIX
    // Lines the file does not take are counted, with why the first was not,
    // and nothing the stream does afterwards clears them: the stopped
    // recording still says so. What part of a line went in is taken back, so
    // the next line starts on its own.
    void aRecordingRemembersTheLinesItCouldNotWrite()
    {
        registerStreamingCodex();
        SettingsStore settings;
        settings.edit([](AppSettings &draft) { draft.speech.providerId = QStringLiteral("codex"); return true; });
        RecordingSession recording(&settings, m_registry.get(), [](QObject *parent) {
            return new FakeAudioInput(parent);
        });
        QSignalSpy stopped(&recording, &RecordingSession::stopped);
        QSignalSpy problems(&recording, &RecordingSession::problemChanged);
        QString error;
        const QString path = startRecording(recording, QString(), &error);
        QVERIFY2(!path.isEmpty(), qPrintable(error));
        m_codex->emitFinalText(QStringLiteral("Written."));
        {
            // The file can grow by a few bytes more: a line's start is
            // written, then its rest fails with EFBIG.
            rlimit savedLimit{};
            QVERIFY(getrlimit(RLIMIT_FSIZE, &savedLimit) == 0);
            rlimit limit = savedLimit;
            limit.rlim_cur = rlim_t(QFileInfo(path).size() + 5);
            QVERIFY(setrlimit(RLIMIT_FSIZE, &limit) == 0);
            const auto savedXfsz = std::signal(SIGXFSZ, SIG_IGN);
            const auto restore = qScopeGuard([&] {
                std::signal(SIGXFSZ, savedXfsz);
                setrlimit(RLIMIT_FSIZE, &savedLimit);
            });
            m_codex->emitFinalText(QStringLiteral("Lost."));
            m_codex->emitFinalText(QStringLiteral("Lost too."));
        }
        // Once, for the first line the file did not take.
        QCOMPARE(problems.count(), 1);
        m_codex->emitFailure(QStringLiteral("Connection reset"), true, SpeechPhase::Streaming,
                             ProviderFailureKind::Network);
        QTRY_COMPARE_WITH_TIMEOUT(m_codex->startCalls, 2, 5000);
        m_codex->emitFinalText(QStringLiteral("Written again."));
        recording.stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 5000);

        const RecordingStatus last = stopped.first().first().value<RecordingStatus>();
        QCOMPARE(last.unwrittenLines, 2);
        QVERIFY2(last.writeError.startsWith(recordingFileError(path, QString())), qPrintable(last.writeError));
        QVERIFY(last.streams.first().problem.isEmpty());
        QCOMPARE(recordedTexts(path), QStringList({QStringLiteral("Written."), QStringLiteral("Written again.")}));
    }
#endif

    // A start whose caller stopped waiting is discarded: the recording ends at
    // once, with both its inputs and streams, and leaves no file.
    void aDiscardedRecordingLeavesNoFile()
    {
        registerStreamingCodex();
        SettingsStore settings;
        settings.edit([](AppSettings &draft) { draft.speech.providerId = QStringLiteral("codex"); return true; });
        bool microphoneStopped = false;
        bool systemAudioStopped = false;
        RecordingSession recording(
            &settings, m_registry.get(),
            [&microphoneStopped](QObject *parent) {
                auto *microphone = new FakeAudioInput(parent);
                microphone->onStop = [&microphoneStopped] { microphoneStopped = true; };
                return microphone;
            },
            [&systemAudioStopped](QObject *parent) {
                auto *systemAudio = new FakeAudioInput(parent);
                systemAudio->onStop = [&systemAudioStopped] { systemAudioStopped = true; };
                return systemAudio;
            });
        QSignalSpy changed(&recording, &RecordingSession::recordingChanged);
        QString error;
        const QString path = startRecording(recording, QString(), &error);
        QVERIFY2(!path.isEmpty(), qPrintable(error));
        FakeSpeechTranscriber *me = streamingCodexes().at(0);
        FakeSpeechTranscriber *them = streamingCodexes().at(1);
        me->emitFinalText(QStringLiteral("Hello?"));
        them->emitFinalText(QStringLiteral("Hi."));
        int meCancels = 0;
        int themCancels = 0;
        me->onCancelAttempt = [&meCancels](quint64) { ++meCancels; };
        them->onCancelAttempt = [&themCancels](quint64) { ++themCancels; };

        recording.discard();
        QVERIFY(!recording.isRecording());
        QVERIFY(microphoneStopped);
        QVERIFY(systemAudioStopped);
        QCOMPARE(meCancels, 1);
        QCOMPARE(themCancels, 1);
        QVERIFY(!QFileInfo::exists(path));
        QCOMPARE(changed.count(), 2);
        QCOMPARE(changed.last().first().toBool(), false);
    }

    // A recording outlasts its sign-in. The next stream, after a rollover or a
    // drop, renews it first when the provider says it is due; one the service
    // turns down unforeseen renews once before the stream counts as stopped.
    void aRecordingRenewsItsSignInBeforeTheNextStream()
    {
        SpeechAttemptPolicy::setStableAttemptMs(0);
        const auto restore = qScopeGuard([] { SpeechAttemptPolicy::setStableAttemptMs(10000); });
        registerStreamingCodex();
        SettingsStore settings;
        settings.edit([](AppSettings &draft) { draft.speech.providerId = QStringLiteral("codex"); return true; });
        RecordingSession recording(&settings, m_registry.get(), [](QObject *parent) {
            return new FakeAudioInput(parent);
        });
        QString error;
        QVERIFY2(!startRecording(recording, QString(), &error).isEmpty(), qPrintable(error));
        QCOMPARE(m_codex->prepareCalls, 1);
        // The token the provider holds works once prepare has run more than
        // this many times. Each stream after the first notes how many it had.
        int expiredUpTo = 1;
        QList<int> preparedAtStarts;
        m_codex->onStartAttempt = [this, &expiredUpTo, &preparedAtStarts] {
            preparedAtStarts << m_codex->prepareCalls;
            if (m_codex->prepareCalls > expiredUpTo) {
                m_codex->emitConnected();
                return;
            }
            m_codex->emitFailure(QStringLiteral("Unauthorized"), false, SpeechPhase::Connect,
                                 ProviderFailureKind::Authentication);
        };
        const auto streamState = [&recording] { return recording.status().streams.first().state; };

        m_codex->refreshRequired = true;
        m_codex->emitCompletion();
        QTRY_COMPARE_WITH_TIMEOUT(preparedAtStarts, QList<int>{2}, 5000);
        QCOMPARE(streamState(), RecordingStreamStatus::State::Recording);

        m_codex->refreshRequired = false;
        expiredUpTo = 2;
        m_codex->emitFailure(QStringLiteral("Unauthorized"), false, SpeechPhase::Streaming,
                             ProviderFailureKind::Authentication);
        QCOMPARE(streamState(), RecordingStreamStatus::State::Reconnecting);
        QTRY_COMPARE_WITH_TIMEOUT(preparedAtStarts, QList<int>({2, 3}), 5000);
        QCOMPARE(streamState(), RecordingStreamStatus::State::Recording);

        // Signed out for good.
        expiredUpTo = 100;
        m_codex->emitFailure(QStringLiteral("Unauthorized"), false, SpeechPhase::Streaming,
                             ProviderFailureKind::Authentication);
        QTRY_COMPARE_WITH_TIMEOUT(streamState(), RecordingStreamStatus::State::Stopped, 5000);
        QCOMPARE(preparedAtStarts, QList<int>({2, 3, 4}));
        QCOMPARE(recording.status().streams.first().problem, QStringLiteral("Unauthorized"));
    }

    // A walk started while a sign-in renews replaces the renewal: it ends in
    // ready(), never in the renewal's answer.
    void aWalkStartedDuringARenewalEndsReady()
    {
        FakeSpeechTranscriber transcriber;
        transcriber.backgroundPrepare = true;
        SpeechChain chain(m_registry.get(), [&transcriber](const QString &) { return &transcriber; });
        QSignalSpy ready(&chain, &SpeechChain::ready);
        QSignalSpy renewed(&chain, &SpeechChain::signInRenewed);
        AppSettings settings;
        settings.speech.providerId = QStringLiteral("claude");

        chain.begin(settings.speech.providerId, {});
        chain.renewSignIn(&transcriber, settings.speech, settings.accounts);
        chain.prepare(settings);

        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 5000);
        QCOMPARE(renewed.count(), 0);
    }

    // A dropped stream is cancelled once its partial is written: whatever it
    // still sends while the recording waits to reconnect is not written.
    void aRecordingWritesNothingMoreFromADroppedStream()
    {
        registerStreamingCodex();
        SettingsStore settings;
        settings.edit([](AppSettings &draft) { draft.speech.providerId = QStringLiteral("codex"); return true; });
        RecordingSession recording(&settings, m_registry.get(), [](QObject *parent) {
            return new FakeAudioInput(parent);
        });
        QString error;
        const QString path = startRecording(recording, QString(), &error);
        QVERIFY2(!path.isEmpty(), qPrintable(error));
        m_codex->emitPartialText(QStringLiteral("Before the drop"));
        const quint64 dropped = m_codex->currentAttemptId;
        m_codex->emitFailure(QStringLiteral("Connection reset"), true, SpeechPhase::Streaming,
                             ProviderFailureKind::Network);
        QVERIFY(m_codex->cancelledAttempts.contains(dropped));
        m_codex->emitFinalText(QStringLiteral("Before the drop."));
        m_codex->emitPartialText(QStringLiteral("Stale"));

        QTRY_COMPARE_WITH_TIMEOUT(m_codex->startCalls, 2, 5000);
        QCOMPARE(recordedTexts(path), QStringList{QStringLiteral("Before the drop")});
    }

    // Utterance ends the first provider passed before it failed to connect
    // reach the fallback that takes the audio from its start.
    void aRecordingFallbackGetsTheUtteranceEndsAgain()
    {
        registerStreamingCodex(false);
        QPointer<FakeSpeechTranscriber> claude;
        int ends = 0;
        m_registry->registerSpeechProvider({QStringLiteral("claude"), QStringLiteral("Claude Voice")},
                                           [&claude, &ends](QObject *parent) {
                                               claude = new FakeSpeechTranscriber(parent);
                                               claude->streamsFinals = true;
                                               claude->onEndUtterance = [&ends] { ++ends; };
                                               return claude.data();
                                           });
        SettingsStore settings;
        settings.edit([](AppSettings &draft) {
            draft.speech.providerId = QStringLiteral("codex");
            draft.speech.fallbackProviderIds = {QStringLiteral("claude")};
            return true;
        });
        QPointer<FakeAudioInput> microphone;
        RecordingSession recording(&settings, m_registry.get(), [&](QObject *parent) {
            microphone = new FakeAudioInput(parent);
            return microphone.data();
        });
        std::optional<QString> outcome;
        recording.start(QString(), {}, m_dir.path(), false, [&outcome](const QString &error) { outcome = error; });
        QTRY_VERIFY_WITH_TIMEOUT(m_codex && m_codex->startCalls == 1, 10000);
        // Two utterances go to Codex while it connects, each ended by the
        // pause after it before the next starts.
        int codexEnds = 0;
        m_codex->onEndUtterance = [&codexEnds] { ++codexEnds; };
        for (int utterance = 0; utterance < 2; ++utterance) {
            microphone->pushAudio(microphoneChunk(8000));
            QTRY_COMPARE_WITH_TIMEOUT(codexEnds, utterance + 1, 5000);
        }
        QCOMPARE(m_codex->audioChunks.size(), 2);
        m_codex->emitFailure(QStringLiteral("Timed out"), true, SpeechPhase::Connect,
                             ProviderFailureKind::Timeout);

        QTRY_VERIFY_WITH_TIMEOUT(claude && claude->startCalls == 1, 10000);
        claude->emitConnected();
        QTRY_COMPARE_WITH_TIMEOUT(ends, 2, 10000);
        QCOMPARE(claude->audioChunks.size(), 2);
        QCOMPARE(outcome, std::optional<QString>(QString()));
    }

    // A provider that never connected hands the audio that waited to the
    // next, even once so much waited that the oldest was dropped.
    void aRecordingFallbackTakesAudioThatOutgrewTheWait()
    {
        registerStreamingCodex(false);
        QPointer<FakeSpeechTranscriber> claude;
        m_registry->registerSpeechProvider({QStringLiteral("claude"), QStringLiteral("Claude Voice")},
                                           [&claude](QObject *parent) {
                                               claude = new FakeSpeechTranscriber(parent);
                                               claude->streamsFinals = true;
                                               return claude.data();
                                           });
        SettingsStore settings;
        settings.edit([](AppSettings &draft) {
            draft.speech.providerId = QStringLiteral("codex");
            draft.speech.fallbackProviderIds = {QStringLiteral("claude")};
            return true;
        });
        QPointer<FakeAudioInput> microphone;
        RecordingSession recording(&settings, m_registry.get(), [&](QObject *parent) {
            microphone = new FakeAudioInput(parent);
            return microphone.data();
        });
        recording.start(QString(), {}, m_dir.path(), false, [](const QString &) {});
        QTRY_VERIFY_WITH_TIMEOUT(m_codex && m_codex->startCalls == 1, 10000);
        // 2 seconds, then 10 minutes, before Codex connects.
        microphone->pushAudio(QByteArray(2 * 32000, '\1'));
        const QByteArray minute(60 * 32000, '\0');
        for (int i = 0; i < 10; ++i) {
            microphone->pushAudio(minute);
        }
        m_codex->emitFailure(QStringLiteral("Timed out"), true, SpeechPhase::Connect,
                             ProviderFailureKind::Timeout);

        QTRY_VERIFY_WITH_TIMEOUT(claude && claude->startCalls == 1, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!claude->audioChunks.isEmpty(), 5000);
        QCOMPARE(claude->audioChunks.first(), QByteArray(3200, '\0'));
    }

    // A stop while the stream is down tries once more at once for the audio
    // that waits; when that fails, the stopped recording says why.
    void aRecordingStoppedWhileDownSaysWhatItMissed()
    {
        registerStreamingCodex();
        SettingsStore settings;
        settings.edit([](AppSettings &draft) { draft.speech.providerId = QStringLiteral("codex"); return true; });
        QPointer<FakeAudioInput> microphone;
        RecordingSession recording(&settings, m_registry.get(), [&](QObject *parent) {
            microphone = new FakeAudioInput(parent);
            return microphone.data();
        });
        QSignalSpy stopped(&recording, &RecordingSession::stopped);
        QString error;
        QVERIFY2(!startRecording(recording, QString(), &error).isEmpty(), qPrintable(error));
        m_codex->onStartAttempt = [this] {
            m_codex->emitFailure(QStringLiteral("Host unreachable"), true, SpeechPhase::Connect,
                                 ProviderFailureKind::Network);
        };
        m_codex->emitFailure(QStringLiteral("Connection reset"), true, SpeechPhase::Streaming,
                             ProviderFailureKind::Network);
        microphone->pushAudio(microphoneChunk(8000));
        const int startsBefore = m_codex->startCalls;

        recording.stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 2000);
        QCOMPARE(m_codex->startCalls, startsBefore + 1);
        const RecordingStreamStatus stream = stopped.first().first().value<RecordingStatus>().streams.first();
        QCOMPARE(stream.state, RecordingStreamStatus::State::Stopped);
        QCOMPARE(stream.problem, QStringLiteral("Host unreachable"));
    }

    // While a stream is down, at most ten minutes of audio wait for it; the
    // oldest goes first, and status says how much.
    void aRecordingKeepsTenMinutesOfAudioWhileDown()
    {
        registerStreamingCodex();
        SettingsStore settings;
        settings.edit([](AppSettings &draft) { draft.speech.providerId = QStringLiteral("codex"); return true; });
        QPointer<FakeAudioInput> microphone;
        RecordingSession recording(&settings, m_registry.get(), [&](QObject *parent) {
            microphone = new FakeAudioInput(parent);
            return microphone.data();
        });
        QString error;
        QVERIFY2(!startRecording(recording, QString(), &error).isEmpty(), qPrintable(error));
        m_codex->emitFailure(QStringLiteral("Connection reset"), true, SpeechPhase::Streaming,
                             ProviderFailureKind::Network);
        // 2 seconds, then 10 minutes, all before the reconnect.
        microphone->pushAudio(QByteArray(2 * 32000, '\1'));
        const QByteArray minute(60 * 32000, '\0');
        for (int i = 0; i < 10; ++i) {
            microphone->pushAudio(minute);
        }
        QCOMPARE(recording.status().streams.first().lostAudioMs, 2000);

        // The stream that returns starts at the newest ten minutes.
        m_codex->audioChunks.clear();
        QTRY_VERIFY_WITH_TIMEOUT(!m_codex->audioChunks.isEmpty(), 5000);
        QCOMPARE(m_codex->audioChunks.first(), QByteArray(3200, '\0'));
        QCOMPARE(recordingStreamProblemText(recording.status().streams.first()),
                 QStringLiteral("The microphone stream was down so long that the oldest 00:00:02 of audio waiting for it was "
                                "dropped."));
    }

    // The microphone is "me" and system audio "them", each with its own
    // provider session, their lines in the one file in the order their texts
    // were finalised: a reply that comes first is written first, even when
    // the question was asked earlier.
    void aRecordingWritesBothStreamsIntoOneFile()
    {
        registerStreamingCodex();
        SettingsStore settings;
        settings.edit([](AppSettings &draft) { draft.speech.providerId = QStringLiteral("codex"); return true; });
        QPointer<FakeAudioInput> microphone;
        QPointer<FakeAudioInput> systemAudio;
        RecordingSession recording(
            &settings, m_registry.get(),
            [&](QObject *parent) { return microphone = new FakeAudioInput(parent); },
            [&](QObject *parent) { return systemAudio = new FakeAudioInput(parent); });
        QSignalSpy stopped(&recording, &RecordingSession::stopped);
        QString error;
        const QString path = startRecording(recording, QString(), &error);
        QVERIFY2(!path.isEmpty(), qPrintable(error));
        QCOMPARE(streamingCodexes().size(), 2);
        FakeSpeechTranscriber *me = streamingCodexes().at(0);
        FakeSpeechTranscriber *them = streamingCodexes().at(1);
        QCOMPARE(recording.status().streams.size(), 2);
        QCOMPARE(recording.status().streams.at(1).speaker, QStringLiteral("them"));

        microphone->pushAudio(microphoneChunk(8000));
        systemAudio->pushAudio(microphoneChunk(4000));
        QTRY_VERIFY_WITH_TIMEOUT(!me->audioChunks.isEmpty() && !them->audioChunks.isEmpty(), 5000);
        QCOMPARE(me->audioChunks.first(), microphoneChunk(8000));
        QCOMPARE(them->audioChunks.first(), microphoneChunk(4000));
        them->emitPartialText(QStringLiteral("Can you look"));
        me->emitFinalText(QStringLiteral("Sorry, go on."));
        them->emitFinalText(QStringLiteral("Can you look at the retry logic?"));
        me->emitFinalText(QStringLiteral("Yes, today."));
        recording.stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 5000);
        QCOMPARE(recordedLines(path), QStringList({QStringLiteral("me: Sorry, go on."),
                                                   QStringLiteral("them: Can you look at the retry logic?"),
                                                   QStringLiteral("me: Yes, today.")}));
    }

    // --mic-only takes the microphone alone: no system audio, no second
    // provider session, one stream. A microphone the system cancels echo in
    // still says when it does not.
    void aMicrophoneOnlyRecordingTakesNoSystemAudio()
    {
        registerStreamingCodex();
        SettingsStore settings;
        settings.edit([](AppSettings &draft) { draft.speech.providerId = QStringLiteral("codex"); return true; });
        int systemAudioInputs = 0;
        QPointer<FakeAudioInput> microphone;
        EchoCancellerLog log;
        RecordingSession recording(
            &settings, m_registry.get(),
            [&](QObject *parent) {
                microphone = new FakeAudioInput(parent);
                microphone->echoWarning = QStringLiteral("Echo cancellation is off: the driver has none.");
                return microphone.data();
            },
            [&](QObject *parent) {
                ++systemAudioInputs;
                return new FakeAudioInput(parent);
            },
            [&](QString *) { return std::make_unique<FakeEchoCanceller>(&log); });
        QString error;
        QVERIFY2(!startRecording(recording, QString(), &error, {}, true).isEmpty(), qPrintable(error));
        QCOMPARE(systemAudioInputs, 0);
        QVERIFY(!microphone->keepsSilence);
        QCOMPARE(streamingCodexes().size(), 1);
        QCOMPARE(recording.status().streams.size(), 1);
        QCOMPARE(recording.status().streams.first().speaker, QStringLiteral("me"));
        QCOMPARE(recording.status().echoCancellationWarning,
                 QStringLiteral("Echo cancellation is off: the driver has none."));
    }

    // While a dictation has the microphone, its stream sends silence and
    // shows paused, and system audio records on; each dictation that listens
    // writes one line saying so. As on Windows, where the system cancels the
    // echo and Speecher has no canceller, the microphone's own warning shows
    // and the microphone skips silence as its settings say.
    void aDictationSilencesTheRecordingsMicrophone()
    {
        registerStreamingCodex();
        SettingsStore settings;
        settings.edit([](AppSettings &draft) { draft.speech.providerId = QStringLiteral("codex"); return true; });
        QPointer<FakeAudioInput> microphone;
        QPointer<FakeAudioInput> systemAudio;
        RecordingSession recording(
            &settings, m_registry.get(),
            [&](QObject *parent) {
                microphone = new FakeAudioInput(parent);
                microphone->echoWarning = echoCancellationNotOfferedText();
                return microphone.data();
            },
            [&](QObject *parent) { return systemAudio = new FakeAudioInput(parent); },
            [](QString *) { return std::unique_ptr<EchoCanceller>(); });
        QSignalSpy problems(&recording, &RecordingSession::problemChanged);
        QString error;
        const QString path = startRecording(recording, QString(), &error);
        QVERIFY2(!path.isEmpty(), qPrintable(error));
        QCOMPARE(recording.status().streams.size(), 2);
        QCOMPARE(recording.status().echoCancellationWarning, echoCancellationNotOfferedText());
        QVERIFY(!microphone->keepsSilence);
        FakeSpeechTranscriber *me = streamingCodexes().at(0);
        FakeSpeechTranscriber *them = streamingCodexes().at(1);

        recording.setDictating(true);
        recording.setDictating(true);
        recording.setDictationListening(true);
        QCOMPARE(recording.status().streams.first().state, RecordingStreamStatus::State::Paused);
        QCOMPARE(problems.count(), 1);
        microphone->pushAudio(microphoneChunk(8000));
        systemAudio->pushAudio(microphoneChunk(4000));
        QTRY_VERIFY_WITH_TIMEOUT(!me->audioChunks.isEmpty() && !them->audioChunks.isEmpty(), 5000);
        QCOMPARE(me->audioChunks.first(), QByteArray(3200, '\0'));
        QCOMPARE(them->audioChunks.first(), microphoneChunk(4000));
        them->emitFinalText(QStringLiteral("Take your time."));

        recording.setDictating(false);
        recording.setDictationListening(false);
        QCOMPARE(recording.status().streams.first().state, RecordingStreamStatus::State::Recording);
        me->audioChunks.clear();
        microphone->pushAudio(microphoneChunk(8000));
        QTRY_VERIFY_WITH_TIMEOUT(!me->audioChunks.isEmpty(), 5000);
        QCOMPARE(me->audioChunks.first(), microphoneChunk(8000));
        recording.setDictating(true);
        recording.setDictationListening(true);
        QCOMPARE(recordedLines(path), QStringList({QStringLiteral("me: (dictating, not on the call)"),
                                                   QStringLiteral("them: Take your time."),
                                                   QStringLiteral("me: (dictating, not on the call)")}));
    }

    // Beside system audio, the microphone goes whole through the echo
    // canceller, which takes system audio as its reference and gives back
    // what it held at the stop; one that cannot start leaves the recording
    // running, the microphone skipping silence as its settings say, and
    // status saying why.
    void aRecordingCancelsTheEchoOfSystemAudio()
    {
        registerStreamingCodex();
        SettingsStore settings;
        settings.edit([](AppSettings &draft) { draft.speech.providerId = QStringLiteral("codex"); return true; });
        QPointer<FakeAudioInput> microphone;
        QPointer<FakeAudioInput> systemAudio;
        EchoCancellerLog log;
        int cancellers = 0;
        const QString warning = echoCancellationFailedText(-1);
        RecordingSession recording(
            &settings, m_registry.get(),
            [&](QObject *parent) { return microphone = new FakeAudioInput(parent); },
            [&](QObject *parent) { return systemAudio = new FakeAudioInput(parent); },
            [&](QString *why) -> std::unique_ptr<EchoCanceller> {
                if (cancellers++ > 0) {
                    *why = warning;
                    return nullptr;
                }
                return std::make_unique<FakeEchoCanceller>(&log, microphoneChunk(0).size());
            });
        QSignalSpy stopped(&recording, &RecordingSession::stopped);
        QString error;
        QVERIFY2(!startRecording(recording, QString(), &error).isEmpty(), qPrintable(error));
        QVERIFY(recording.status().echoCancellationWarning.isEmpty());
        QVERIFY(microphone->keepsSilence);
        systemAudio->pushAudio(microphoneChunk(4000));
        QCOMPARE(log.references, QList<QByteArray>{microphoneChunk(4000)});
        FakeSpeechTranscriber *me = streamingCodexes().at(0);
        me->autoCompleteOnFinish = false;
        microphone->pushAudio(microphoneChunk(8000));
        recording.stop();
        QCOMPARE(log.flushes, 1);
        // The input ends once all its audio has gone, the canceller's last
        // with it.
        QTRY_COMPARE_WITH_TIMEOUT(me->stopCalls, 1, 5000);
        QCOMPARE(me->audioChunks.join(), microphoneChunk(8000));
        me->emitCompletion();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 5000);

        QVERIFY2(!startRecording(recording, QString(), &error).isEmpty(), qPrintable(error));
        QCOMPARE(recording.status().echoCancellationWarning, warning);
        QCOMPARE(recording.status().streams.size(), 2);
        QVERIFY(!microphone->keepsSilence);
    }

    // System audio that cannot start, or whose stream stops, leaves the
    // microphone recording; status says why.
    void aFailedSystemAudioStreamLeavesTheMicrophoneRecording()
    {
        registerStreamingCodex();
        SettingsStore settings;
        settings.edit([](AppSettings &draft) { draft.speech.providerId = QStringLiteral("codex"); return true; });
        bool systemAudioStarts = false;
        RecordingSession recording(
            &settings, m_registry.get(), [](QObject *parent) { return new FakeAudioInput(parent); },
            [&](QObject *parent) {
                auto *systemAudio = new FakeAudioInput(parent);
                systemAudio->startResult = systemAudioStarts;
                systemAudio->startError = QStringLiteral("No default output.");
                return systemAudio;
            });
        QSignalSpy stopped(&recording, &RecordingSession::stopped);
        QSignalSpy problems(&recording, &RecordingSession::problemChanged);
        QString error;
        const QString path = startRecording(recording, QString(), &error);
        QVERIFY2(!path.isEmpty(), qPrintable(error));
        const RecordingStreamStatus them = recording.status().streams.at(1);
        QCOMPARE(them.state, RecordingStreamStatus::State::Stopped);
        QCOMPARE(them.problem, QStringLiteral("No default output."));
        streamingCodexes().first()->emitFinalText(QStringLiteral("Still here."));
        recording.stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 5000);
        QCOMPARE(recordedLines(path), QStringList{QStringLiteral("me: Still here.")});

        systemAudioStarts = true;
        QVERIFY2(!startRecording(recording, QString(), &error).isEmpty(), qPrintable(error));
        problems.clear();
        streamingCodexes().last()->emitFailure(QStringLiteral("Unsupported audio"), false, SpeechPhase::Streaming,
                                      ProviderFailureKind::Other);
        QVERIFY(recording.isRecording());
        QCOMPARE(recording.status().streams.at(1).state, RecordingStreamStatus::State::Stopped);
        QCOMPARE(recording.status().streams.at(1).problem, QStringLiteral("Unsupported audio"));
        QCOMPARE(recording.status().streams.first().state, RecordingStreamStatus::State::Recording);
        QCOMPARE(problems.count(), 1);
    }

    // While a dictation has the microphone the echo canceller is given
    // silence, so the call speech it held from before the dictation still
    // reaches the provider and none of the dictation does, through a pause
    // and a resume.
    void aDictationSilencesWhatGoesIntoTheEchoCanceller()
    {
        registerStreamingCodex();
        SettingsStore settings;
        settings.edit([](AppSettings &draft) { draft.speech.providerId = QStringLiteral("codex"); return true; });
        QPointer<FakeAudioInput> microphone;
        EchoCancellerLog log;
        RecordingSession recording(
            &settings, m_registry.get(),
            [&](QObject *parent) { return microphone = new FakeAudioInput(parent); },
            [](QObject *parent) { return new FakeAudioInput(parent); },
            [&](QString *) { return std::make_unique<FakeEchoCanceller>(&log, microphoneChunk(0).size()); });
        QString error;
        QVERIFY2(!startRecording(recording, QString(), &error).isEmpty(), qPrintable(error));
        FakeSpeechTranscriber *me = streamingCodexes().at(0);
        me->autoCompleteOnFinish = false;
        const QByteArray beforeDictation = microphoneChunk(1000);
        const QByteArray dictated = microphoneChunk(8000);
        const QByteArray whilePaused = microphoneChunk(2000);
        const QByteArray afterDictation = microphoneChunk(3000);
        const QByteArray silence(dictated.size(), '\0');

        microphone->pushAudio(beforeDictation);
        recording.setDictating(true);
        microphone->pushAudio(dictated);
        microphone->pushAudio(dictated);
        recording.setDictating(false);
        microphone->pushAudio(whilePaused);
        recording.setDictating(true);
        microphone->pushAudio(dictated);
        recording.setDictating(false);
        microphone->pushAudio(afterDictation);
        recording.stop();
        // The input ends once all its audio has gone.
        QTRY_COMPARE_WITH_TIMEOUT(me->stopCalls, 1, 5000);
        QCOMPARE(me->audioChunks.join(),
                 beforeDictation + silence + silence + whilePaused + silence + afterDictation);
    }

    // Speech that only the stop releases, held by the echo canceller, still
    // begins an utterance, so the Custom Endpoint uploads it.
    void aRecordingStopUploadsTheSpeechTheEchoCancellerHeld()
    {
        FakeServer server;
        server.route("POST /v1/audio/transcriptions", httpResponse("200 OK", "application/json", "{\"text\":\"Yes.\"}"));
        SettingsStore settings;
        useEndpoint(settings, server);
        QPointer<FakeAudioInput> microphone;
        EchoCancellerLog log;
        RecordingSession recording(
            &settings, m_registry.get(),
            [&](QObject *parent) { return microphone = new FakeAudioInput(parent); },
            [](QObject *parent) { return new FakeAudioInput(parent); },
            [&](QString *) { return std::make_unique<FakeEchoCanceller>(&log, microphoneChunk(0).size()); });
        QSignalSpy stopped(&recording, &RecordingSession::stopped);
        QString error;
        const QString path = startRecording(recording, QString(), &error);
        QVERIFY2(!path.isEmpty(), qPrintable(error));

        microphone->pushAudio(microphoneChunk(8000));
        recording.stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 3000);
        QCOMPARE(recordedTexts(path), QStringList{QStringLiteral("Yes.")});
        QCOMPARE(server.requests.size(), 1);
    }

    // A stream that stops part way through an utterance forgets it, so a
    // recording started within the pause after still begins its first.
    void aRecordingStartedRightAfterAStoppedStreamBeginsItsFirstUtterance()
    {
        registerStreamingCodex();
        SettingsStore settings;
        settings.edit([](AppSettings &draft) { draft.speech.providerId = QStringLiteral("codex"); return true; });
        QPointer<FakeAudioInput> systemAudio;
        RecordingSession recording(
            &settings, m_registry.get(), [](QObject *parent) { return new FakeAudioInput(parent); },
            [&](QObject *parent) { return systemAudio = new FakeAudioInput(parent); });
        QSignalSpy stopped(&recording, &RecordingSession::stopped);
        QString error;
        QVERIFY2(!startRecording(recording, QString(), &error).isEmpty(), qPrintable(error));
        systemAudio->pushAudio(microphoneChunk(8000));
        streamingCodexes().at(1)->emitFailure(QStringLiteral("Unsupported audio"), false, SpeechPhase::Streaming,
                                              ProviderFailureKind::Other);
        QCOMPARE(recording.status().streams.at(1).state, RecordingStreamStatus::State::Stopped);
        recording.stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 5000);

        QVERIFY2(!startRecording(recording, QString(), &error).isEmpty(), qPrintable(error));
        FakeSpeechTranscriber *them = streamingCodexes().last();
        systemAudio->pushAudio(microphoneChunk(8000));
        QTRY_COMPARE_WITH_TIMEOUT(them->beginUtteranceCalls, 1, 2000);
    }

    // System audio that stops, or never starts, leaves the echo canceller
    // nothing to cancel: what it held comes out once, the microphone's audio
    // then passes as it comes, and status says echo cancellation is off.
    void aStoppedSystemAudioTurnsEchoCancellationOff()
    {
        registerStreamingCodex();
        SettingsStore settings;
        settings.edit([](AppSettings &draft) { draft.speech.providerId = QStringLiteral("codex"); return true; });
        QPointer<FakeAudioInput> microphone;
        QPointer<FakeAudioInput> systemAudio;
        EchoCancellerLog log;
        bool systemAudioStarts = true;
        RecordingSession recording(
            &settings, m_registry.get(),
            [&](QObject *parent) { return microphone = new FakeAudioInput(parent); },
            [&](QObject *parent) {
                systemAudio = new FakeAudioInput(parent);
                systemAudio->startResult = systemAudioStarts;
                return systemAudio.data();
            },
            [&](QString *) { return std::make_unique<FakeEchoCanceller>(&log, microphoneChunk(0).size()); });
        QSignalSpy stopped(&recording, &RecordingSession::stopped);
        QString error;
        QVERIFY2(!startRecording(recording, QString(), &error).isEmpty(), qPrintable(error));
        FakeSpeechTranscriber *me = streamingCodexes().at(0);
        microphone->pushAudio(microphoneChunk(1000));
        QSignalSpy problems(&recording, &RecordingSession::problemChanged);

        systemAudio->emitFailure(QStringLiteral("The sound server went away."));
        QCOMPARE(log.flushes, 1);
        QCOMPARE(recording.status().echoCancellationWarning, echoCancellationWithoutSystemAudioText());
        QCOMPARE(recording.status().streams.at(1).state, RecordingStreamStatus::State::Stopped);
        QCOMPARE(recording.status().streams.at(1).problem, QStringLiteral("The sound server went away."));
        QCOMPARE(problems.count(), 1);
        microphone->pushAudio(microphoneChunk(2000));
        QTRY_COMPARE_WITH_TIMEOUT(me->audioChunks.join(), microphoneChunk(1000) + microphoneChunk(2000), 5000);
        recording.stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 5000);
        QCOMPARE(log.flushes, 1);

        systemAudioStarts = false;
        QVERIFY2(!startRecording(recording, QString(), &error).isEmpty(), qPrintable(error));
        QCOMPARE(log.flushes, 2);
        QCOMPARE(recording.status().echoCancellationWarning, echoCancellationWithoutSystemAudioText());
    }

    // A provider that refuses system audio's stream, as one may refuse a
    // second session, stops only that stream: system audio keeps coming, so
    // the echo canceller keeps taking it out of the microphone, and status
    // says nothing of echo. Once system audio itself stops, the canceller
    // stops too and status says so.
    void aRefusedSystemAudioStreamKeepsCancellingEcho()
    {
        registerStreamingCodex(false);
        SettingsStore settings;
        settings.edit([](AppSettings &draft) { draft.speech.providerId = QStringLiteral("codex"); return true; });
        QPointer<FakeAudioInput> systemAudio;
        EchoCancellerLog log;
        RecordingSession recording(
            &settings, m_registry.get(), [](QObject *parent) { return new FakeAudioInput(parent); },
            [&](QObject *parent) { return systemAudio = new FakeAudioInput(parent); },
            [&](QString *) { return std::make_unique<FakeEchoCanceller>(&log); });
        QSignalSpy stopped(&recording, &RecordingSession::stopped);
        std::optional<QString> outcome;
        recording.start(QString(), {}, m_dir.path(), false, [&outcome](const QString &error) { outcome = error; });
        QTRY_COMPARE_WITH_TIMEOUT(streamingCodexes().size(), 2, 10000);
        streamingCodexes().at(0)->emitConnected();
        streamingCodexes().at(1)->emitFailure(QStringLiteral("Too many sessions"), false, SpeechPhase::Connect,
                                              ProviderFailureKind::Other);
        QCOMPARE(outcome, std::optional<QString>(QString()));
        QCOMPARE(recording.status().streams.at(1).state, RecordingStreamStatus::State::Stopped);
        QCOMPARE(recording.status().streams.at(1).problem, QStringLiteral("Too many sessions"));
        QVERIFY(recording.status().echoCancellationWarning.isEmpty());
        QVERIFY(systemAudio->active);
        systemAudio->pushAudio(microphoneChunk(4000));
        QCOMPARE(log.references, QList<QByteArray>{microphoneChunk(4000)});
        QCOMPARE(log.flushes, 0);

        QSignalSpy problems(&recording, &RecordingSession::problemChanged);
        systemAudio->emitFailure(QStringLiteral("The sound server went away."));
        QCOMPARE(log.flushes, 1);
        QCOMPARE(recording.status().echoCancellationWarning, echoCancellationWithoutSystemAudioText());
        QCOMPARE(problems.count(), 1);
        recording.stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 5000);
    }

    // A dictation that takes the microphone while the recording starts, after
    // the microphone's stream connected, shows as paused once it runs.
    void aDictationDuringTheStartShowsOnceTheRecordingRuns()
    {
        registerStreamingCodex(false);
        SettingsStore settings;
        settings.edit([](AppSettings &draft) { draft.speech.providerId = QStringLiteral("codex"); return true; });
        RecordingSession recording(
            &settings, m_registry.get(), [](QObject *parent) { return new FakeAudioInput(parent); },
            [](QObject *parent) { return new FakeAudioInput(parent); });
        std::optional<QString> outcome;
        recording.start(QString(), {}, m_dir.path(), false, [&outcome](const QString &error) { outcome = error; });
        QTRY_COMPARE_WITH_TIMEOUT(streamingCodexes().size(), 2, 10000);
        streamingCodexes().at(0)->emitConnected();
        recording.setDictating(true);
        streamingCodexes().at(1)->emitConnected();
        QCOMPARE(outcome, std::optional<QString>(QString()));
        QCOMPARE(recording.status().streams.first().state, RecordingStreamStatus::State::Paused);
    }

    // One line per dictation that listens while a recording runs: one that
    // already listened, and is paused, when the recording starts gets its
    // line then and none at its resume; a start whose microphone failed gets
    // none; one still starting when the recording does gets its line once it
    // listens, not also when the recording starts.
    void aRecordingWritesOneLinePerDictationThatListens()
    {
        registerStreamingCodex(false);
        SettingsStore settings;
        settings.edit([](AppSettings &draft) { draft.speech.providerId = QStringLiteral("codex"); return true; });
        RecordingSession recording(
            &settings, m_registry.get(), [](QObject *parent) { return new FakeAudioInput(parent); },
            [](QObject *parent) { return new FakeAudioInput(parent); });
        QSignalSpy stopped(&recording, &RecordingSession::stopped);
        const QString marker = QStringLiteral("me: (dictating, not on the call)");
        const auto startAndConnect = [&](const QString &path) {
            const qsizetype streams = streamingCodexes().size();
            std::optional<QString> outcome;
            recording.start(path, {}, m_dir.path(), false, [&outcome](const QString &error) { outcome = error; });
            QTRY_COMPARE_WITH_TIMEOUT(streamingCodexes().size(), streams + 2, 10000);
            streamingCodexes().at(streams)->emitConnected();
            streamingCodexes().at(streams + 1)->emitConnected();
            QCOMPARE(outcome, std::optional<QString>(QString()));
        };

        recording.setDictating(true);
        recording.setDictationListening(true);
        recording.setDictating(false);
        const QString paused = m_dir.filePath(QStringLiteral("paused.md"));
        startAndConnect(paused);
        QCOMPARE(recordedLines(paused), QStringList{marker});
        recording.setDictating(true);
        recording.setDictating(false);
        recording.setDictationListening(false);
        recording.setDictating(true);
        recording.setDictating(false);
        QCOMPARE(recordedLines(paused), QStringList{marker});
        recording.stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 5000);

        recording.setDictating(true);
        const QString starting = m_dir.filePath(QStringLiteral("starting.md"));
        startAndConnect(starting);
        QCOMPARE(recordedLines(starting), QStringList());
        recording.setDictationListening(true);
        QCOMPARE(recordedLines(starting), QStringList{marker});
    }

    // A stop waits for both streams: the lines of one that answers after the
    // other finished are written before the recording ends.
    void aRecordingStopWritesBothStreamsLastLinesBeforeItEnds()
    {
        registerStreamingCodex();
        SettingsStore settings;
        settings.edit([](AppSettings &draft) { draft.speech.providerId = QStringLiteral("codex"); return true; });
        RecordingSession recording(
            &settings, m_registry.get(), [](QObject *parent) { return new FakeAudioInput(parent); },
            [](QObject *parent) { return new FakeAudioInput(parent); });
        QString error;
        const QString path = startRecording(recording, QString(), &error);
        QVERIFY2(!path.isEmpty(), qPrintable(error));
        QStringList linesAtStop;
        QSignalSpy stopped(&recording, &RecordingSession::stopped);
        connect(&recording, &RecordingSession::stopped, this, [&] { linesAtStop = recordedLines(path); });
        FakeSpeechTranscriber *me = streamingCodexes().at(0);
        FakeSpeechTranscriber *them = streamingCodexes().at(1);
        me->autoCompleteOnFinish = false;
        them->autoCompleteOnFinish = false;

        recording.stop();
        QTRY_VERIFY_WITH_TIMEOUT(me->stopCalls == 1 && them->stopCalls == 1, 5000);
        me->emitFinalText(QStringLiteral("Talk soon."));
        me->emitCompletion();
        QCOMPARE(stopped.count(), 0);
        them->emitFinalText(QStringLiteral("Bye."));
        them->emitCompletion();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 5000);
        QCOMPARE(linesAtStop, QStringList({QStringLiteral("me: Talk soon."), QStringLiteral("them: Bye.")}));
    }

    // A stop's deadline gives up on the stream still waiting; the one that
    // finished reports nothing missing.
    void aRecordingStopGivesUpOnlyOnTheSlowStream()
    {
        registerStreamingCodex();
        SettingsStore settings;
        settings.edit([](AppSettings &draft) { draft.speech.providerId = QStringLiteral("codex"); return true; });
        RecordingSession recording(
            &settings, m_registry.get(), [](QObject *parent) { return new FakeAudioInput(parent); },
            [](QObject *parent) { return new FakeAudioInput(parent); });
        QSignalSpy stopped(&recording, &RecordingSession::stopped);
        QString error;
        QVERIFY2(!startRecording(recording, QString(), &error).isEmpty(), qPrintable(error));
        FakeSpeechTranscriber *me = streamingCodexes().at(0);
        FakeSpeechTranscriber *them = streamingCodexes().at(1);
        me->utteranceAnswerTimeout = 300;
        them->utteranceAnswerTimeout = 300;
        them->autoCompleteOnFinish = false;

        recording.stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 3000);
        const RecordingStatus status = stopped.first().first().value<RecordingStatus>();
        QCOMPARE(status.streams.at(0).problem, QString());
        QCOMPARE(status.streams.at(1).problem, recordingStopTimedOutText());
    }

    // The microphone's stream failing to connect fails the start while system
    // audio's still connects: that stream is cancelled and no file is left.
    void aMicrophoneStreamThatFailsWhileSystemAudioConnectsLeavesNoFile()
    {
        registerStreamingCodex(false);
        SettingsStore settings;
        settings.edit([](AppSettings &draft) { draft.speech.providerId = QStringLiteral("codex"); return true; });
        RecordingSession recording(
            &settings, m_registry.get(), [](QObject *parent) { return new FakeAudioInput(parent); },
            [](QObject *parent) { return new FakeAudioInput(parent); });
        const QString path = m_dir.filePath(QStringLiteral("never.md"));
        std::optional<QString> outcome;
        recording.start(path, {}, m_dir.path(), false, [&outcome](const QString &error) { outcome = error; });
        QTRY_COMPARE_WITH_TIMEOUT(streamingCodexes().size(), 2, 10000);
        int themCancels = 0;
        streamingCodexes().at(1)->onCancelAttempt = [&themCancels](quint64) { ++themCancels; };

        streamingCodexes().at(0)->emitFailure(QStringLiteral("Too many sessions"), true, SpeechPhase::Connect,
                                              ProviderFailureKind::Network);
        QCOMPARE(outcome, std::optional<QString>(QStringLiteral("Too many sessions")));
        QCOMPARE(themCancels, 1);
        QVERIFY(!QFileInfo::exists(path));
        QVERIFY(!recording.isRecording());
    }

    void numbersASaveThatWouldOverwrite()
    {
        const QString audio = m_dir.filePath(QStringLiteral("talk.mp3.wav"));
        writeWav(audio);
        const QString folder = m_dir.filePath(QStringLiteral("out"));
        QVERIFY(QDir().mkpath(folder));
        for (const QString &taken : {QStringLiteral("talk.mp3-transcribed.txt"),
                                     QStringLiteral("talk.mp3-transcribed (2).txt")}) {
            QFile existing(QDir(folder).filePath(taken));
            QVERIFY(existing.open(QIODevice::WriteOnly));
        }
        SettingsStore settings;
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        TranscribeOptions options = speechOnly();
        options.destination = TranscriptDestination::Folder;
        options.folder = folder;

        QVERIFY(session.start({audio}, options));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);

        const auto results = finished.first().first().value<QList<TranscribeFileResult>>();
        QCOMPARE(results.first().savedPath,
                 QDir(folder).filePath(QStringLiteral("talk.mp3-transcribed (3).txt")));
        QVERIFY(readFile(results.first().savedPath).startsWith(QStringLiteral("heard ")));
    }

    void cancelSkipsTheRestAndStillFinishes()
    {
        const QString first = m_dir.filePath(QStringLiteral("one.wav"));
        const QString second = m_dir.filePath(QStringLiteral("two.wav"));
        writeWav(first);
        writeWav(second);
        SettingsStore settings;
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy started(&session, &FileTranscriptionSession::fileStarted);
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        QObject::connect(&session, &FileTranscriptionSession::fileProgress, &session,
                         [&session] { session.cancel(); });

        QVERIFY(session.start({first, second}, speechOnly()));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        QTest::qWait(200);

        QCOMPARE(started.count(), 1);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.first().at(1).toBool(), true);
        QVERIFY(finished.first().first().value<QList<TranscribeFileResult>>().isEmpty());
        QVERIFY(!session.isRunning());
        QVERIFY(!QFile::exists(m_dir.filePath(QStringLiteral("one-transcribed.txt"))));
    }

    // A listening batch cancelled as its transcript clears, as it starts, or
    // while its microphone starts, leaves the microphone off and starts no
    // provider.
    void aListeningBatchCancelledAsItStartsGoesNoFurther_data()
    {
        QTest::addColumn<QString>("when");
        QTest::newRow("as the transcript clears") << QStringLiteral("cleared");
        QTest::newRow("as the file starts") << QStringLiteral("started");
        QTest::newRow("while the microphone starts") << QStringLiteral("microphone");
    }

    void aListeningBatchCancelledAsItStartsGoesNoFurther()
    {
        QFETCH(QString, when);
        SettingsStore settings;
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        FakeAudioInput microphone;
        const auto cancel = [&session] { session.cancel(); };
        if (when == QStringLiteral("cleared")) {
            QObject::connect(&session, &FileTranscriptionSession::filePartialText, &session, cancel);
        } else if (when == QStringLiteral("started")) {
            QObject::connect(&session, &FileTranscriptionSession::fileStarted, &session, cancel);
        } else {
            microphone.onStart = cancel;
        }

        QVERIFY(session.startListening(&microphone, speechOnly()));
        QTest::qWait(200);

        QVERIFY(!session.isRunning());
        QVERIFY(!microphone.active);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.first().at(1).toBool(), true);
        QCOMPARE(m_script.attempts, 0);
    }

    // A listening batch cancelled while its microphone stops, with another
    // started as it reports the cancel: the stop that returns ends nothing
    // of the new batch, which keeps listening.
    void aBatchStartedAsAStoppingOneIsCancelledKeepsListening()
    {
        SettingsStore settings;
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        FakeAudioInput first;
        FakeAudioInput second;
        first.onStop = [&session] { session.cancel(); };
        QObject::connect(&session, &FileTranscriptionSession::batchFinished, &session, [&] {
            QVERIFY(session.startListening(&second, speechOnly()));
        }, Qt::SingleShotConnection);
        QVERIFY(session.startListening(&first, speechOnly()));
        QTRY_COMPARE_WITH_TIMEOUT(m_script.attempts, 1, 10000);

        session.finishListening();
        QTest::qWait(200);

        QCOMPARE(finished.count(), 1);
        QVERIFY(session.isRunning());
        QVERIFY(second.active);
        session.cancel();
    }

    // A listening batch started while a cancel stops the microphone begins
    // once the cancel is done, so that stop leaves its capture on; a second
    // cancel ends it once.
    void aBatchStartedWhileACancelStopsTheMicrophoneWaitsForIt()
    {
        SettingsStore settings;
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        FakeAudioInput microphone;
        QVERIFY(session.startListening(&microphone, speechOnly()));
        bool restarted = false;
        microphone.onStop = [&] {
            if (!std::exchange(restarted, true)) {
                QVERIFY(session.startListening(&microphone, speechOnly()));
            }
        };

        session.cancel();
        QTest::qWait(100);

        QVERIFY(restarted);
        QCOMPARE(finished.count(), 1);
        QVERIFY(session.isRunning());
        QVERIFY(microphone.active);
        session.cancel();
        session.cancel();
        QCOMPARE(finished.count(), 2);
    }

    // A batch cancelled as its stream clears the transcript for the provider
    // that is ready starts no attempt on the provider it released.
    void aCancelAsTheStreamBeginsStartsNoAttempt()
    {
        SettingsStore settings;
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        int partials = 0;
        // The file's start clears the transcript first, then the stream as
        // its provider is ready.
        QObject::connect(&session, &FileTranscriptionSession::filePartialText, &session, [&] {
            if (++partials == 2) {
                session.cancel();
            }
        });
        FakeAudioInput microphone;
        QVERIFY(session.startListening(&microphone, speechOnly()));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        QTest::qWait(100);

        QCOMPARE(finished.count(), 1);
        QVERIFY(!session.isRunning());
        QCOMPARE(m_script.attempts, 0);
    }

    // A batch cancelled while a failed file stops the microphone finishes
    // once, cancelled, without reporting the file.
    void aCancelWhileAFailedFileStopsTheMicrophoneFinishesOnce()
    {
        SettingsStore settings;
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy files(&session, &FileTranscriptionSession::fileFinished);
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        FakeAudioInput microphone;
        microphone.onStop = [&session] { session.cancel(); };
        QVERIFY(session.startListening(&microphone, speechOnly()));

        microphone.emitFailure(QStringLiteral("unplugged"));
        QTest::qWait(100);

        QCOMPARE(files.count(), 0);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.first().at(1).toBool(), true);
        QVERIFY(!session.isRunning());
    }

    void withoutVocabularyTheProviderHearsNone()
    {
        const QString audio = m_dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        SettingsStore settings;
        settings.edit([](AppSettings &draft) { draft.vocabulary = {{QStringLiteral("Speecher")}}; return true; });
        QVERIFY(!settings.snapshot().speech.vocabulary.isEmpty());
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        TranscribeOptions options = speechOnly();
        options.applyVocabulary = false;

        QVERIFY(session.start({audio}, options));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);

        QCOMPARE(m_script.attempts, 1);
        QVERIFY(m_script.vocabulary.isEmpty());
    }

    // A term only the run adds reaches the speech provider ahead of the saved
    // ones, and refinement, while the saved vocabulary stays as it was.
    void addedVocabularyIsForThisBatchOnly()
    {
        const QString audio = m_dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        SettingsStore settings;
        settings.edit([](AppSettings &draft) { draft.vocabulary = {{QStringLiteral("Speecher")}}; return true; });
        const QList<VocabularyEntry> saved = settings.dictationSnapshot().vocabulary;
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        TranscribeOptions options = speechOnly();
        options.refinementProviderId = QStringLiteral("openai");
        options.cleanupStrength = QStringLiteral("balanced");
        options.addedVocabulary = {QStringLiteral("readSharedChoice")};

        QVERIFY(session.start({audio}, options));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);

        QCOMPARE(m_script.vocabulary, QStringList({QStringLiteral("readSharedChoice"), QStringLiteral("Speecher")}));
        QVERIFY(m_refinedVocabulary.contains(QStringLiteral("readSharedChoice")));
        QCOMPARE(settings.dictationSnapshot().vocabulary, saved);
    }

    // A long file's terms lead the speech request and refinement in the
    // file's order, ahead of a saved term with Priority and uses, as many as
    // each takes, and the batch starts without delay.
    void addedVocabularyLeadsInItsOwnOrder()
    {
        const QString audio = m_dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        SettingsStore settings;
        VocabularyEntry favourite{QStringLiteral("Speecher")};
        favourite.starred = true;
        favourite.frequency = 5;
        settings.edit([&](AppSettings &draft) { draft.vocabulary = {favourite}; return true; });
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        TranscribeOptions options = speechOnly();
        options.refinementProviderId = QStringLiteral("openai");
        options.cleanupStrength = QStringLiteral("balanced");
        for (int index = 20000; index > 0; --index) {
            options.addedVocabulary.append(QStringLiteral("term%1").arg(index));
        }
        QElapsedTimer elapsed;
        elapsed.start();

        QVERIFY(session.start({audio}, options));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);

        // Gathering the terms used to take half a minute for this many.
        QVERIFY(elapsed.elapsed() < 5000);

        QCOMPARE(m_script.vocabulary, options.addedVocabulary.first(VocabularyLimit::maxKeyterms));
        QCOMPARE(m_refinedVocabulary, options.addedVocabulary.first(VocabularyLimit::maxRefinementTerms));
    }

    // The run's terms and the saved ones are capped together: a saved term
    // too long to follow a longer saved one still goes when the run's term
    // leaves it room.
    void addedVocabularySharesOneCapWithTheSavedTerms()
    {
        const QString audio = m_dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        const auto words = [](const QString &word, int count) {
            return QStringList(count, word).join(QLatin1Char(' '));
        };
        VocabularyEntry longer{words(QStringLiteral("long"), 300)};
        longer.frequency = 2;
        VocabularyEntry shorter{words(QStringLiteral("short"), 250)};
        shorter.frequency = 1;
        SettingsStore settings;
        settings.edit([&](AppSettings &draft) { draft.vocabulary = {longer, shorter}; return true; });
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        TranscribeOptions options = speechOnly();
        options.addedVocabulary = {words(QStringLiteral("added"), 201)};

        QVERIFY(session.start({audio}, options));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);

        QCOMPARE(m_script.vocabulary, QStringList({options.addedVocabulary.first(), shorter.term}));
    }

    // Without the saved vocabulary only the run's terms go, and a run's term
    // the saved list limits to another profile, or keeps from the speech
    // service, goes anyway.
    void addedVocabularyOverridesTheSavedEntry()
    {
        const QString audio = m_dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        SettingsStore settings;
        VocabularyEntry elsewhere{QStringLiteral("readSharedChoice")};
        elsewhere.profiles = {WritingProfile::Email};
        elsewhere.keyTerm = false;
        settings.edit([&](AppSettings &draft) {
            draft.vocabulary = {{QStringLiteral("Speecher")}, elsewhere};
            return true;
        });
        TranscribeOptions options = speechOnly();
        options.refinementProviderId = QStringLiteral("openai");
        options.cleanupStrength = QStringLiteral("balanced");
        options.writingProfile = WritingProfile::AiCoding;
        options.addedVocabulary = {QStringLiteral("readSharedChoice")};

        for (const bool applyVocabulary : {true, false}) {
            FileTranscriptionSession session(&settings, m_registry.get());
            QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
            options.applyVocabulary = applyVocabulary;
            m_refinedVocabulary.clear();

            QVERIFY(session.start({audio}, options));
            QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);

            const QStringList expected = applyVocabulary
                ? QStringList({QStringLiteral("readSharedChoice"), QStringLiteral("Speecher")})
                : QStringList({QStringLiteral("readSharedChoice")});
            QCOMPARE(m_script.vocabulary, expected);
            QCOMPARE(m_refinedVocabulary, expected);
        }
    }

    void savingNowhereWritesNothingAndStillDelivers()
    {
        QTemporaryDir dir;
        const QString audio = dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        SettingsStore settings;
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        TranscribeOptions options = speechOnly();
        options.destination = TranscriptDestination::None;

        QVERIFY(session.start({audio}, options));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);

        const auto results = finished.first().first().value<QList<TranscribeFileResult>>();
        QVERIFY(results.first().refined.startsWith(QStringLiteral("heard ")));
        QVERIFY(results.first().savedPath.isEmpty());
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files), QStringList({QStringLiteral("memo.wav")}));
    }

    // The bar climbs through the phases in order, never backwards, and stays
    // short of the end however long a wait runs: only fileFinished fills it.
    void fileProgressOnlyEndsWhenTheFileDoes()
    {
        for (bool refines : {true, false}) {
            const QList<qreal> samples{
                overallFileProgress(0.0, TranscribePhase::Reading, refines, 0),
                overallFileProgress(0.0, TranscribePhase::Reading, refines, 60000),
                overallFileProgress(0.0, TranscribePhase::Transcribing, refines, 0),
                overallFileProgress(0.5, TranscribePhase::Transcribing, refines, 0),
                overallFileProgress(1.0, TranscribePhase::Transcribing, refines, 0),
                overallFileProgress(1.0, TranscribePhase::Finishing, refines, 0),
                overallFileProgress(1.0, TranscribePhase::Finishing, refines, 2000),
                overallFileProgress(1.0, TranscribePhase::Finishing, refines, 600000),
                overallFileProgress(1.0, TranscribePhase::Refining, refines, 0),
                overallFileProgress(1.0, TranscribePhase::Refining, refines, 2000),
                overallFileProgress(1.0, TranscribePhase::Refining, refines, 600000),
            };
            for (int i = 1; i < samples.size(); ++i) {
                QVERIFY2(samples.at(i) >= samples.at(i - 1), qPrintable(QString::number(i)));
            }
            QCOMPARE(samples.first(), 0.0);
            QVERIFY(samples.last() < 1.0);
        }
        // Without a refinement pass, sending and finishing take its share.
        QCOMPARE(overallFileProgress(1.0, TranscribePhase::Transcribing, true, 0), 0.75);
        QCOMPARE(overallFileProgress(1.0, TranscribePhase::Transcribing, false, 0), 0.90);
        QVERIFY(overallFileProgress(1.0, TranscribePhase::Finishing, true, 600000) <= 0.80);
        QVERIFY(overallFileProgress(1.0, TranscribePhase::Finishing, false, 600000) <= 0.97);
    }

    // A stream that restarts can report less audio sent than before; what a
    // bar shows holds its highest point until the next file starts.
    void shownProgressNeverMovesBack()
    {
        ForwardProgress shown;
        QCOMPARE(shown.advance(overallFileProgress(0.6, TranscribePhase::Transcribing, true, 0)), 0.47);
        QCOMPARE(shown.advance(overallFileProgress(0.2, TranscribePhase::Transcribing, true, 0)), 0.47);
        QCOMPARE(shown.advance(overallFileProgress(0.8, TranscribePhase::Transcribing, true, 0)), 0.61);
        shown = {};
        QCOMPARE(shown.advance(overallFileProgress(0.0, TranscribePhase::Reading, true, 0)), 0.0);
    }

    void headlessRunSavesPrintsAndReportsFailure()
    {
        QTemporaryDir dir;
        const QString audio = dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        const QString broken = dir.filePath(QStringLiteral("broken.wav"));
        QFile file(broken);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("not audio at all");
        file.close();
        SettingsStore settings;
        HeadlessTranscribeOptions options;
        options.speechProviderId = QStringLiteral("claude");
        options.refinementProviderId = QStringLiteral("openai");
        options.cleanupStrength = QStringLiteral("balanced");
        options.json = true;
        std::istringstream in;
        std::ostringstream out;
        std::ostringstream err;

        QCOMPARE(runHeadlessTranscribe({audio}, options, &settings, m_registry.get(), in, out, err, false), 0);
        QStringList lines = QString::fromStdString(out.str()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 2);
        const QJsonObject result = QJsonDocument::fromJson(lines.at(0).toUtf8()).object();
        QCOMPARE(result.value(QStringLiteral("file")).toString(), audio);
        QCOMPARE(result.value(QStringLiteral("ok")).toBool(), true);
        QCOMPARE(result.value(QStringLiteral("text")).toString(), QStringLiteral("Heard it."));
        const QString saved = dir.filePath(QStringLiteral("memo-transcribed.txt"));
        QCOMPARE(result.value(QStringLiteral("saved")).toString(), saved);
        QCOMPARE(readFile(saved), QStringLiteral("Heard it."));
        QCOMPARE(QJsonDocument::fromJson(lines.at(1).toUtf8()).object(),
                 QJsonObject({{QStringLiteral("summary"), true}, {QStringLiteral("files"), 1},
                              {QStringLiteral("succeeded"), 1}, {QStringLiteral("failed"), 0}}));
        QVERIFY(QString::fromStdString(err.str()).contains(QStringLiteral("saved ")));

        // Raw text to stdout, saved nowhere; the unreadable file fails alone.
        options.json = false;
        options.printTranscripts = true;
        options.raw = true;
        options.destination = TranscriptDestination::None;
        out.str({});
        QCOMPARE(runHeadlessTranscribe({broken, audio}, options, &settings, m_registry.get(), in, out, err, false), 1);
        const QString printed = QString::fromStdString(out.str());
        QVERIFY2(printed.startsWith(QStringLiteral("# memo.wav\n\nheard ")), qPrintable(printed));
        QVERIFY(!printed.contains(QStringLiteral("broken.wav")));
        QVERIFY(QString::fromStdString(err.str()).contains(QStringLiteral("broken.wav: failed: ")));
        QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("memo-transcribed (2).txt"))));
    }

    // `transcribe --profile` runs the profile's own refinement provider, with
    // the model it picked; `--refine` still replaces it.
    void headlessRunUsesTheProfilesServicesUnlessTold()
    {
        QString anthropicModel;
        m_registry->registerRefinementProvider({QStringLiteral("anthropic"), QStringLiteral("Anthropic")},
                                               [&anthropicModel](QObject *parent) {
                                                   auto *refiner = new FakeRefiner(parent);
                                                   refiner->providerId = QStringLiteral("anthropic");
                                                   refiner->autoComplete = true;
                                                   refiner->autoCompleteText = QStringLiteral("Anthropic heard it.");
                                                   connect(refiner, &TranscriptRefiner::completed, parent,
                                                           [refiner, &anthropicModel] {
                                                               anthropicModel = refiner->lastAnthropicModel;
                                                           });
                                                   return refiner;
                                               });
        QTemporaryDir dir;
        const QString audio = dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        SettingsStore settings;
        AppSettings stored = settings.snapshot();
        stored.refinement.providerId = QStringLiteral("openai");
        stored.refinement.writingProfiles[1].refinementProvider = QStringLiteral("anthropic");
        stored.refinement.writingProfiles[1].refinementModel = QStringLiteral("claude-sonnet-5-5");
        settings.applySnapshot(stored);
        HeadlessTranscribeOptions options;
        options.writingProfile = WritingProfile::Email;
        options.destination = TranscriptDestination::None;
        options.json = true;
        std::istringstream in;
        std::ostringstream out;
        std::ostringstream err;
        const auto text = [&out] {
            const QString first = QString::fromStdString(out.str()).section(QLatin1Char('\n'), 0, 0);
            return QJsonDocument::fromJson(first.toUtf8()).object().value(QStringLiteral("text")).toString();
        };

        QCOMPARE(runHeadlessTranscribe({audio}, options, &settings, m_registry.get(), in, out, err, false), 0);
        QCOMPARE(text(), QStringLiteral("Anthropic heard it."));
        QCOMPARE(anthropicModel, QStringLiteral("claude-sonnet-5-5"));

        options.refinementProviderId = QStringLiteral("openai");
        out.str({});
        QCOMPARE(runHeadlessTranscribe({audio}, options, &settings, m_registry.get(), in, out, err, false), 0);
        QCOMPARE(text(), QStringLiteral("Heard it."));

        // A profile's speech service this build lacks is passed over for the
        // settings' one, not refused as an unknown --model.
        stored = settings.snapshot();
        stored.refinement.writingProfiles[1].speechProvider = QStringLiteral("local");
        settings.applySnapshot(stored);
        out.str({});
        QCOMPARE(runHeadlessTranscribe({audio}, options, &settings, m_registry.get(), in, out, err, false), 0);
        QCOMPARE(text(), QStringLiteral("Heard it."));
    }

    // A transcript that could not be saved fails its file; a run that cannot
    // start says why instead of exiting quietly.
    void headlessRunFailsUnsavedTranscriptsAndRefusalsOutLoud()
    {
        QTemporaryDir dir;
        const QString audio = dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        SettingsStore settings;
        HeadlessTranscribeOptions options;
        options.speechProviderId = QStringLiteral("claude");
        options.refinementProviderId = QStringLiteral("none");
        options.destination = TranscriptDestination::Folder;
        options.folder = dir.filePath(QStringLiteral("gone"));
        options.json = true;
        std::istringstream in;
        std::ostringstream out;
        std::ostringstream err;

        QCOMPARE(runHeadlessTranscribe({audio}, options, &settings, m_registry.get(), in, out, err, false), 1);
        QStringList lines = QString::fromStdString(out.str()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 2);
        const QJsonObject result = QJsonDocument::fromJson(lines.at(0).toUtf8()).object();
        QCOMPARE(result.value(QStringLiteral("ok")).toBool(), false);
        QVERIFY(result.value(QStringLiteral("error")).toString().startsWith(QStringLiteral("Could not save")));
        QCOMPARE(QJsonDocument::fromJson(lines.at(1).toUtf8()).object().value(QStringLiteral("failed")).toInt(), 1);
        QVERIFY(QString::fromStdString(err.str()).contains(QStringLiteral("memo.wav: failed: Could not save")));

        out.str({});
        err.str({});
        QCOMPARE(runHeadlessTranscribe({}, options, &settings, m_registry.get(), in, out, err, false), 2);
        QVERIFY(!err.str().empty());
        QVERIFY(QJsonDocument::fromJson(QByteArray::fromStdString(out.str())).object().value(QStringLiteral("summary")).toBool());

        // Files a run never reached did not succeed.
        out.str({});
        options.speechProviderId = QStringLiteral("nonexistent");
        QCOMPARE(runHeadlessTranscribe({audio, audio}, options, &settings, m_registry.get(), in, out, err, false), 2);
        QCOMPARE(QJsonDocument::fromJson(QByteArray::fromStdString(out.str())).object().value(QStringLiteral("succeeded")).toInt(), 0);
    }

    // `transcribe -` names the piped audio stdin, fails on empty stdin, and
    // removes the spooled audio either way.
    void headlessRunReadsAudioFromStdin()
    {
        QTemporaryDir dir;
        QTemporaryDir spool;
        const auto restoreTemporaryFolder = redirectTemporaryFolder(spool.path());
        const QString audio = dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        QFile wav(audio);
        QVERIFY(wav.open(QIODevice::ReadOnly));
        SpoolRecordingInput audioInput(wav.readAll().toStdString(), spool.path());
        std::istream in(&audioInput);
        SettingsStore settings;
        HeadlessTranscribeOptions options;
        options.speechProviderId = QStringLiteral("claude");
        options.refinementProviderId = QStringLiteral("none");
        options.destination = TranscriptDestination::Folder;
        options.folder = dir.path();
        options.json = true;
        std::ostringstream out;
        std::ostringstream err;

        QCOMPARE(runHeadlessTranscribe({kStdinFile}, options, &settings, m_registry.get(), in, out, err, false), 0);
        const QJsonObject result =
            QJsonDocument::fromJson(QByteArray::fromStdString(out.str().substr(0, out.str().find('\n')))).object();
        QCOMPARE(result.value(QStringLiteral("file")).toString(), QStringLiteral("-"));
        QCOMPARE(result.value(QStringLiteral("ok")).toBool(), true);
        QCOMPARE(result.value(QStringLiteral("saved")).toString(), dir.filePath(QStringLiteral("stdin-transcribed.txt")));
        QVERIFY2(QString::fromStdString(err.str()).contains(QStringLiteral("stdin: saved ")), err.str().c_str());
        // A log says once that stdin is being read.
        QCOMPARE(QString::fromStdString(err.str()).count(QStringLiteral("stdin: Reading")), 1);
        QCOMPARE(audioInput.spooled(), QStringList{QStringLiteral("stdin")});
        QVERIFY(QDir(spool.path()).isEmpty());

        SpoolRecordingInput emptyInput({}, spool.path());
        std::istream empty(&emptyInput);
        out.str({});
        err.str({});
        QCOMPARE(runHeadlessTranscribe({kStdinFile}, options, &settings, m_registry.get(), empty, out, err, false), 1);
        QCOMPARE(QString::fromStdString(err.str()), QStringLiteral("stdin: Reading the audio\u2026\nNo audio on stdin\n"));
        const QJsonObject summary = QJsonDocument::fromJson(QByteArray::fromStdString(out.str())).object();
        QCOMPARE(summary.value(QStringLiteral("failed")).toInt(), 1);
        QCOMPARE(summary.value(QStringLiteral("succeeded")).toInt(), 0);
        QCOMPARE(emptyInput.spooled(), QStringList{QStringLiteral("stdin")});
        QVERIFY(QDir(spool.path()).isEmpty());
    }

#ifdef Q_OS_UNIX
    // A disk that fills while stdin is spooled fails the run, rather than
    // transcribing what fit. A file size limit stands in for the full disk.
    void headlessRunFailsWhenStdinDoesNotFit()
    {
        QTemporaryDir spool;
        const auto restoreTemporaryFolder = redirectTemporaryFolder(spool.path());
        rlimit savedLimit{};
        QVERIFY(getrlimit(RLIMIT_FSIZE, &savedLimit) == 0);
        constexpr rlim_t kLimit = 64 * 1024;
        rlimit limit = savedLimit;
        limit.rlim_cur = kLimit;
        QVERIFY(setrlimit(RLIMIT_FSIZE, &limit) == 0);
        // Over the limit a write fails with EFBIG instead of ending the process.
        const auto savedXfsz = std::signal(SIGXFSZ, SIG_IGN);
        const auto restore = qScopeGuard([&] {
            std::signal(SIGXFSZ, savedXfsz);
            setrlimit(RLIMIT_FSIZE, &savedLimit);
        });
        // Fills the limit, then leaves a tail that only the last flush writes.
        SpoolRecordingInput tooLong(std::string(kLimit + 100, 'a'), spool.path());
        std::istream in(&tooLong);
        SettingsStore settings;
        HeadlessTranscribeOptions options;
        options.speechProviderId = QStringLiteral("claude");
        options.refinementProviderId = QStringLiteral("none");
        options.printTranscripts = true;
        options.destination = TranscriptDestination::None;
        std::ostringstream out;
        std::ostringstream err;

        QCOMPARE(runHeadlessTranscribe({kStdinFile}, options, &settings, m_registry.get(), in, out, err, false), 1);
        QVERIFY2(QString::fromStdString(err.str()).contains(QStringLiteral("\nCould not read stdin: ")),
                 err.str().c_str());
        QVERIFY(out.str().empty());
        QCOMPARE(tooLong.spooled(), QStringList{QStringLiteral("stdin")});
        QVERIFY(QDir(spool.path()).isEmpty());
    }
#endif

    // Subtitles are saved and printed in place of text, and fail a file whose
    // speech provider returned no timings with the window's reason.
    void headlessRunWritesSubtitlesOnlyFromTimings()
    {
        QTemporaryDir dir;
        const QString audio = dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        SettingsStore settings;
        HeadlessTranscribeOptions options;
        options.speechProviderId = QStringLiteral("claude");
        options.refinementProviderId = QStringLiteral("openai");
        options.cleanupStrength = QStringLiteral("balanced");
        options.format = TranscriptFormat::Srt;
        options.printTranscripts = true;
        m_script.segments = {{0, 1000, QStringLiteral("heard words")}};
        std::istringstream in;
        std::ostringstream out;
        std::ostringstream err;

        QCOMPARE(runHeadlessTranscribe({audio}, options, &settings, m_registry.get(), in, out, err, false), 0);
        // Subtitles come from the timings, so nothing is refined.
        QVERIFY(m_refinedWith.isEmpty());
        const QString srt = QStringLiteral("1\n00:00:00,000 --> 00:00:01,000\nheard words");
        QFile saved(dir.filePath(QStringLiteral("memo-transcribed.srt")));
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(saved.readAll()), srt + QLatin1Char('\n'));
        QCOMPARE(QString::fromStdString(out.str()), srt + QLatin1Char('\n'));

        options.format = TranscriptFormat::WebVtt;
        options.printTranscripts = false;
        options.json = true;
        out.str({});
        QCOMPARE(runHeadlessTranscribe({audio}, options, &settings, m_registry.get(), in, out, err, false), 0);
        const QJsonObject vtt = QJsonDocument::fromJson(out.str().substr(0, out.str().find('\n')).c_str()).object();
        QCOMPARE(vtt.value(QStringLiteral("saved")).toString(), dir.filePath(QStringLiteral("memo-transcribed.vtt")));
        QCOMPARE(vtt.value(QStringLiteral("text")).toString(),
                 QStringLiteral("WEBVTT\n\n00:00:00.000 --> 00:00:01.000\nheard words"));

        m_script.segments.clear();
        options.format = TranscriptFormat::Srt;
        options.json = false;
        options.printTranscripts = true;
        out.str({});
        err.str({});
        QCOMPARE(runHeadlessTranscribe({audio}, options, &settings, m_registry.get(), in, out, err, false), 1);
        QVERIFY(out.str().empty());
        QVERIFY2(QString::fromStdString(err.str())
                     .contains(QStringLiteral("memo.wav: failed: Subtitles need timings, and Scripted returned none.")),
                 err.str().c_str());
        QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("memo-transcribed (2).srt"))));
    }

    // Each video container the pickers offer: its audio track decodes and
    // reaches the speech provider as half a second of 16 kHz audio.
    void decodesTheAudioTrackOfVideoContainers_data()
    {
        QTest::addColumn<QString>("extension");
        QTest::addColumn<QMediaFormat::FileFormat>("container");
        QTest::addColumn<QMediaFormat::AudioCodec>("audioCodec");
        QTest::addColumn<QMediaFormat::VideoCodec>("videoCodec");
        using A = QMediaFormat::AudioCodec;
        using V = QMediaFormat::VideoCodec;
        QTest::newRow("mp4") << QStringLiteral("mp4") << QMediaFormat::MPEG4 << A::AAC << V::MPEG4;
        QTest::newRow("m4v") << QStringLiteral("m4v") << QMediaFormat::MPEG4 << A::AAC << V::MPEG4;
        QTest::newRow("mov") << QStringLiteral("mov") << QMediaFormat::QuickTime << A::AAC << V::MPEG4;
        // FLAC, which FFmpeg encodes itself: on Windows its AAC encoder is
        // Media Foundation's, whose timestamps the Matroska muxer rejects,
        // leaving a clip with a fraction of its audio.
        QTest::newRow("mkv") << QStringLiteral("mkv") << QMediaFormat::Matroska << A::FLAC << V::MPEG4;
        QTest::newRow("webm") << QStringLiteral("webm") << QMediaFormat::WebM << A::Opus << V::AV1;
        QTest::newRow("avi") << QStringLiteral("avi") << QMediaFormat::AVI << A::AC3 << V::MPEG4;
    }

    void decodesTheAudioTrackOfVideoContainers()
    {
        QFETCH(QString, extension);
        QFETCH(QMediaFormat::FileFormat, container);
        QFETCH(QMediaFormat::AudioCodec, audioCodec);
        QFETCH(QMediaFormat::VideoCodec, videoCodec);
        QVERIFY(transcribableExtensions().contains(extension));
#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
        Q_UNUSED(container);
        Q_UNUSED(audioCodec);
        Q_UNUSED(videoCodec);
        QSKIP("Writing the clips needs Qt 6.8's recorder inputs");
#else
        // Only a clip this Qt's media backend says it cannot write is skipped;
        // once it says it can, failing to write or read it fails the test.
        QMediaFormat format(container);
        format.setAudioCodec(audioCodec);
        format.setVideoCodec(videoCodec);
        if (!format.isSupported(QMediaFormat::Encode)) {
            QSKIP("This Qt cannot write this clip");
        }
        const QString clip = m_dir.filePath(QStringLiteral("clip.") + extension);
        const QString recordError = recordClip(clip, container, audioCodec, videoCodec);
        QVERIFY2(recordError.isEmpty(), qPrintable(recordError));
        QVERIFY(QMimeDatabase().mimeTypeForFile(clip).name().startsWith(QStringLiteral("video/")));
        QVERIFY(isAudioFile(clip));
        SettingsStore settings;
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);

        QVERIFY(session.start({clip}, speechOnly()));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);

        const auto results = finished.first().first().value<QList<TranscribeFileResult>>();
        QVERIFY2(!results.first().failed(), qPrintable(results.first().error));
        // Half a second of 16 kHz mono s16 is 16000 bytes; encoders pad the
        // start and end by a frame or two.
        QVERIFY2(m_script.bytes > 14000 && m_script.bytes < 20000, qPrintable(QString::number(m_script.bytes)));
#endif
    }

    // A dropped or opened file need not be one the pickers list: any audio or
    // video gets its try with the decoder. Other files do not.
    void acceptsAnyAudioOrVideoFile_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<bool>("accepted");
        QTest::newRow("ogv") << QStringLiteral("talk.ogv") << true;
        QTest::newRow("mpg") << QStringLiteral("talk.mpg") << true;
        QTest::newRow("aiff") << QStringLiteral("talk.aiff") << true;
        QTest::newRow("text") << QStringLiteral("talk.txt") << false;
    }

    void acceptsAnyAudioOrVideoFile()
    {
        QFETCH(QString, name);
        QFETCH(bool, accepted);
        QVERIFY(!transcribableExtensions().contains(QFileInfo(name).suffix()));
        // Empty, so the type comes from the name alone.
        QFile file(m_dir.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        QCOMPARE(isAudioFile(file.fileName()), accepted);
    }

    // "Open with" offers Speecher for exactly the files its pickers take.
    void packagingRegistersTheTranscribableTypes()
    {
        // The MIME types of those files, with the aliases older
        // shared-mime-info releases and file managers still use.
        const QStringList mimeTypes{
            QStringLiteral("audio/vnd.wave"), QStringLiteral("audio/wav"), QStringLiteral("audio/x-wav"),
            QStringLiteral("audio/mpeg"), QStringLiteral("audio/mp4"), QStringLiteral("audio/x-m4a"),
            QStringLiteral("audio/aac"), QStringLiteral("audio/flac"), QStringLiteral("audio/ogg"),
            QStringLiteral("audio/x-vorbis+ogg"), QStringLiteral("audio/x-opus+ogg"), QStringLiteral("audio/webm"),
            QStringLiteral("video/webm"), QStringLiteral("video/mp4"), QStringLiteral("video/x-m4v"),
            QStringLiteral("video/quicktime"), QStringLiteral("video/x-matroska"), QStringLiteral("video/vnd.avi"),
            QStringLiteral("video/x-msvideo")};
        const QStringList extensions = sorted(transcribableExtensions());
        const QStringList desktop = packagingEntries(QStringLiteral("io.github.firemonster612.speecher.desktop"),
                                                     QStringLiteral("^MimeType=(.*)$"));
        QCOMPARE(desktop.size(), 1);
        QCOMPARE(sorted(desktop.first().split(u';', Qt::SkipEmptyParts)), sorted(mimeTypes));
        QCOMPARE(packagingEntries(QStringLiteral("macos/Info.plist.in"),
                                  QStringLiteral("^\\t\\t\\t\\t<string>(\\w+)</string>$")),
                 extensions);
        QCOMPARE(packagingEntries(QStringLiteral("windows/speecher.iss"),
                                  QStringLiteral("Classes\\\\\\.(\\w+)\\\\OpenWithProgids")),
                 extensions);
        QCOMPARE(packagingEntries(QStringLiteral("windows/speecher.iss"),
                                  QStringLiteral("SupportedTypes\"; ValueType: string; ValueName: \"\\.(\\w+)\"")),
                 extensions);
        // Each extension's type is one the .desktop file registers, under its
        // own name, an alias or a type it inherits: the databases disagree on
        // which name is canonical (the one Qt bundles for macOS and Windows
        // calls .aac audio/x-aac, shared-mime-info calls it audio/aac).
        const QMimeDatabase mimes;
        for (const QString &extension : extensions) {
            const QMimeType type = mimes.mimeTypeForFile(QStringLiteral("x.") + extension, QMimeDatabase::MatchExtension);
            const bool registered = std::any_of(mimeTypes.cbegin(), mimeTypes.cend(), [&](const QString &name) {
                const QMimeType known = mimes.mimeTypeForName(name);
                return known.isValid() && (type == known || type.inherits(known.name()));
            });
            QVERIFY2(registered, qPrintable(extension + QStringLiteral(" is ") + type.name()));
        }
    }

    // The Transcribe page's queue lists each file the decoder may take once,
    // by its full path, and learns its length.
    void theQueueListsEachAudioFileOnceByItsPath()
    {
        const QString audio = m_dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        const QString notes = m_dir.filePath(QStringLiteral("notes.txt"));
        QFile file(notes);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("not audio");
        file.close();
        ApplicationController controller(true);
        ControllerQueue queue(controller);

        queue.addFiles({audio, m_dir.path() + QStringLiteral("/./memo.wav"), notes});

        QCOMPARE(queue.files(), QStringList{audio});
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(queue.listedFile(audio).durationMs - 500) <= 50, 10000);
        queue.removeFile(audio);
        QVERIFY(queue.files().isEmpty());
    }

    // A batch runs through the controller with the choices it started with,
    // whatever is chosen meanwhile, and ends on its results.
    void theQueueRunsABatchWithTheChoicesItStartedWith()
    {
        const QString first = m_dir.filePath(QStringLiteral("one.wav"));
        const QString second = m_dir.filePath(QStringLiteral("two.wav"));
        writeWav(first);
        writeWav(second);
        ApplicationController controller(true);
        ControllerQueue queue(controller);
        useScriptedSpeech(controller);
        queue.addFiles({first, second});
        queue.setOptions(queueOptions());

        QVERIFY(queue.start());
        QCOMPARE(queue.step(), TranscribeQueue::Step::Transcribe);
        TranscribeOptions changed = queueOptions();
        changed.speechProviderId = QStringLiteral("codex");
        queue.setOptions(changed);
        QTRY_COMPARE_WITH_TIMEOUT(queue.step(), TranscribeQueue::Step::Export, 10000);

        QCOMPARE(queue.batchOptions().speechProviderId, QStringLiteral("claude"));
        QCOMPARE(queue.batchLabels().speech, QStringLiteral("Scripted"));
        QVERIFY(!queue.batchRefines());
        QCOMPARE(queue.results().size(), 2);
        QCOMPARE(queue.results().at(1).path, second);
        QVERIFY(queue.results().at(1).raw.startsWith(QStringLiteral("heard ")));
        QCOMPARE(queue.fileState(1), TranscribeQueue::FileState::Done);
        QCOMPARE(queue.progress(), 1.0);
        QVERIFY(!queue.cancelled());
    }

    // A start while a batch runs is refused before anything changes.
    void aBusyQueueRefusesToStartAndKeepsItsBatch()
    {
        const QString audio = m_dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        ApplicationController controller(true);
        ControllerQueue queue(controller);
        useScriptedSpeech(controller);
        queue.addFiles({audio});
        queue.setOptions(queueOptions());
        QVERIFY(queue.start());

        QVERIFY(!queue.start());

        QCOMPARE(queue.problem(), QStringLiteral("Files are already being transcribed."));
        QCOMPARE(queue.step(), TranscribeQueue::Step::Transcribe);
        QCOMPARE(queue.batch(), QStringList{audio});
        QTRY_COMPARE_WITH_TIMEOUT(queue.step(), TranscribeQueue::Step::Export, 10000);
        QCOMPARE(queue.results().size(), 1);
    }

    // While the refusal it is given holds, as during a dictation, a start
    // runs nothing and says why.
    void aRefusedQueueStartsNothing()
    {
        const QString audio = m_dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        SettingsStore settings;
        FileTranscriptionSession session(&settings, m_registry.get());
        const QString dictating = QStringLiteral("Finish the dictation in progress, then transcribe the files.");
        TranscribeQueue queue(&settings, m_registry.get(), &session, [&dictating] { return dictating; });
        queue.addFiles({audio});

        QVERIFY(!queue.start());

        QCOMPARE(queue.problem(), dictating);
        QCOMPARE(queue.step(), TranscribeQueue::Step::Configure);
        QVERIFY(!session.isRunning());
    }

    // A dictation that starts while the queue announces its batch still stops
    // the batch from running.
    void aDictationStartedDuringStartRefusesTheBatch()
    {
        const QString audio = m_dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        SettingsStore settings;
        FileTranscriptionSession session(&settings, m_registry.get());
        QString refusal;
        TranscribeQueue queue(&settings, m_registry.get(), &session, [&refusal] { return refusal; });
        queue.addFiles({audio});
        const QString dictating = QStringLiteral("Finish the dictation in progress, then transcribe the files.");
        connect(&queue, &TranscribeQueue::stepChanged, &queue, [&] { refusal = dictating; });

        QVERIFY(!queue.start());

        QCOMPARE(queue.problem(), dictating);
        QCOMPARE(queue.step(), TranscribeQueue::Step::Configure);
        QVERIFY(!session.isRunning());
    }

    // Cancelled after a file finished, the batch shows that file's result;
    // back at setup, the file it never reached is still listed.
    void aCancelledBatchKeepsTheFilesItFinished()
    {
        const QString first = m_dir.filePath(QStringLiteral("one.wav"));
        const QString second = m_dir.filePath(QStringLiteral("two.wav"));
        writeWav(first);
        writeWav(second);
        ApplicationController controller(true);
        ControllerQueue queue(controller);
        useScriptedSpeech(controller);
        queue.addFiles({first, second});
        queue.setOptions(queueOptions());
        connect(controller.fileTranscription(), &FileTranscriptionSession::fileStarted, &queue,
                [&queue](int index) {
                    if (index == 1) {
                        queue.cancel();
                    }
                });

        QVERIFY(queue.start());
        QTRY_COMPARE_WITH_TIMEOUT(queue.step(), TranscribeQueue::Step::Export, 10000);

        QVERIFY(queue.cancelled());
        QCOMPARE(queue.results().size(), 1);
        QCOMPARE(queue.fileState(1), TranscribeQueue::FileState::Waiting);
        queue.backToSetup();
        QCOMPARE(queue.step(), TranscribeQueue::Step::Configure);
        QCOMPARE(queue.files(), QStringList{second});
    }

    // Cancelled before any file finished, there is nothing to show.
    void aBatchCancelledEarlyGoesBackToSetup()
    {
        const QString audio = m_dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        ApplicationController controller(true);
        ControllerQueue queue(controller);
        useScriptedSpeech(controller);
        queue.addFiles({audio});
        queue.setOptions(queueOptions());
        connect(controller.fileTranscription(), &FileTranscriptionSession::fileProgress, &queue,
                [&queue] { queue.cancel(); });

        QVERIFY(queue.start());

        QTRY_COMPARE_WITH_TIMEOUT(queue.step(), TranscribeQueue::Step::Configure, 10000);
        QCOMPARE(queue.files(), QStringList{audio});
        QVERIFY(queue.results().isEmpty());
    }

    // A retry runs the failed file again with the batch's choices, its
    // language included when the settings changed since, and puts its result
    // in the failed one's place, leaving the others alone.
    void aRetryReplacesOnlyTheFailedResult()
    {
        const QString broken = m_dir.filePath(QStringLiteral("broken.wav"));
        QFile file(broken);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("not audio at all");
        file.close();
        const QString good = m_dir.filePath(QStringLiteral("good.wav"));
        writeWav(good);
        ApplicationController controller(true);
        ControllerQueue queue(controller);
        useScriptedSpeech(controller);
        // Listed by name, as a dropped file the decoder may still read.
        queue.addFiles({broken, good});
        queue.setOptions(queueOptions());
        QVERIFY(queue.start());
        QTRY_COMPARE_WITH_TIMEOUT(queue.step(), TranscribeQueue::Step::Export, 10000);
        QCOMPARE(queue.fileState(0), TranscribeQueue::FileState::Failed);
        const TranscribeFileResult kept = queue.results().at(1);
        writeWav(broken);
        queue.setOptions({});
        AppSettings settings = controller.settings()->snapshot();
        const QString batchLanguage = settings.speech.language;
        settings.speech.language = QStringLiteral("fr");
        controller.settings()->applySnapshot(settings);

        queue.retry(0);

        QCOMPARE(queue.retrying(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(queue.retrying(), -1, 10000);
        QCOMPARE(m_script.language, batchLanguage);
        QCOMPARE(queue.step(), TranscribeQueue::Step::Export);
        QVERIFY(!queue.results().at(0).failed());
        QCOMPARE(queue.results().at(0).path, broken);
        QVERIFY(qAbs(queue.listedFile(broken).durationMs - 500) <= 50);
        QCOMPARE(queue.results().at(1).raw, kept.raw);
        QCOMPARE(queue.results().at(1).path, good);
    }

    // A Writing Profile brings its services, cleanup level and tone, as for
    // dictation; one that translates refines even when set to None.
    void aProfileBringsItsCleanupAndTone()
    {
        ApplicationController controller(true);
        AppSettings settings = controller.settings()->snapshot();
        for (WritingProfileSettings &profile : settings.refinement.writingProfiles) {
            if (profile.profile == WritingProfile::Work) {
                profile.cleanupStrength = QStringLiteral("strong_polish");
                profile.tone = QStringLiteral("formal");
            } else if (profile.profile == WritingProfile::Personal) {
                profile.cleanupStrength = QStringLiteral("none");
                profile.outputLanguage = QStringLiteral("Spanish");
            }
        }
        controller.settings()->applySnapshot(settings);
        ControllerQueue queue(controller);

        queue.chooseProfile(WritingProfile::Work);
        QCOMPARE(queue.options().cleanupStrength, QStringLiteral("strong_polish"));
        QCOMPARE(queue.options().tone, QStringLiteral("formal"));
        QCOMPARE(queue.options().speechProviderId, settings.speech.providerId);

        queue.chooseProfile(WritingProfile::Personal);
        QCOMPARE(queue.options().cleanupStrength, QStringLiteral("light_cleanup"));
    }

    // Models chosen for a batch run it in place of the settings' own, which
    // keep theirs, and a new profile puts them back.
    void aBatchsOwnModelsLeaveTheSettingsAlone()
    {
        ApplicationController controller(true);
        AppSettings settings = controller.settings()->snapshot();
        settings.refinement.providerId = QStringLiteral("anthropic");
        settings.refinement.anthropicModel = QStringLiteral("claude-sonnet-5-5");
        controller.settings()->applySnapshot(settings);
        settings = controller.settings()->snapshot();
        ControllerQueue queue(controller);

        TranscribeOptions chosen = queue.options();
        chosen.speechProviderId = QStringLiteral("local");
        chosen.speechModel = QStringLiteral("whisper-small");
        chosen.refinementProviderId = QStringLiteral("anthropic");
        chosen.refinementModel = QStringLiteral("claude-opus-5-5");
        queue.setOptions(chosen);

        QCOMPARE(queue.speechModel(), QStringLiteral("whisper-small"));
        QCOMPARE(queue.refinementModel(), QStringLiteral("claude-opus-5-5"));
        QCOMPARE(controller.settings()->snapshot().speech.local.modelId, settings.speech.local.modelId);
        QCOMPARE(controller.settings()->snapshot().refinement.anthropicModel, QStringLiteral("claude-sonnet-5-5"));

        queue.chooseProfile(WritingProfile::Work);
        QVERIFY(queue.options().speechModel.isEmpty());
        QCOMPARE(queue.refinementModel(), QStringLiteral("claude-sonnet-5-5"));
    }

    // Choices nobody has made yet follow the settings; once one is made
    // here, the settings no longer move them.
    void untouchedChoicesFollowTheSettings()
    {
        ApplicationController controller(true);
        ControllerQueue queue(controller);
        AppSettings settings = controller.settings()->snapshot();
        settings.speech.providerId = QStringLiteral("codex");
        controller.settings()->applySnapshot(settings);
        QCOMPARE(queue.options().speechProviderId, QStringLiteral("codex"));

        TranscribeOptions chosen = queue.options();
        chosen.tone = QStringLiteral("casual");
        queue.setOptions(chosen);
        settings.speech.providerId = QStringLiteral("claude");
        controller.settings()->applySnapshot(settings);
        QCOMPARE(queue.options().speechProviderId, QStringLiteral("codex"));
        QCOMPARE(queue.options().tone, QStringLiteral("casual"));
    }

    // A profile, cleanup level or tone deleted after it was chosen gives way
    // to the profile's own instead of reaching the refiner.
    void aDeletedChoiceFallsBackToTheProfiles()
    {
        ApplicationController controller(true);
        AppSettings settings = controller.settings()->snapshot();
        settings.refinement.customTones = {{QStringLiteral("custom_pirate"), QStringLiteral("Pirate"), {}}};
        settings.refinement.customCleanupLevels = {{QStringLiteral("custom_terse"), QStringLiteral("Terse")}};
        WritingProfileSettings notes;
        notes.profile = QStringLiteral("custom_notes");
        notes.name = QStringLiteral("Notes");
        notes.cleanupStrength = QStringLiteral("strong_polish");
        notes.tone = QStringLiteral("formal");
        settings.refinement.writingProfiles << notes;
        for (WritingProfileSettings &profile : settings.refinement.writingProfiles) {
            if (profile.profile == WritingProfile::Work) {
                profile.cleanupStrength = QStringLiteral("light_cleanup");
                profile.tone = QStringLiteral("casual");
            }
        }
        controller.settings()->applySnapshot(settings);
        ControllerQueue queue(controller);
        queue.chooseProfile(WritingProfile::Work);
        TranscribeOptions chosen = queue.options();
        chosen.cleanupStrength = QStringLiteral("custom_terse");
        chosen.tone = QStringLiteral("custom_pirate");
        queue.setOptions(chosen);

        settings.refinement.customTones.clear();
        settings.refinement.customCleanupLevels.clear();
        controller.settings()->applySnapshot(settings);

        QCOMPARE(queue.options().writingProfile, WritingProfile::Work);
        QCOMPARE(queue.options().cleanupStrength, QStringLiteral("light_cleanup"));
        QCOMPARE(queue.options().tone, QStringLiteral("casual"));

        queue.chooseProfile(notes.profile);
        settings.refinement.writingProfiles.removeLast();
        controller.settings()->applySnapshot(settings);

        QCOMPARE(queue.options().writingProfile, WritingProfile::Other);
        const WritingProfileSettings other =
            writingProfileSettingsFor(settings.refinement.writingProfiles, WritingProfile::Other);
        QCOMPARE(queue.options().tone, other.tone);
    }

private:
    // The controller's batches hear through the scripted provider.
    void useScriptedSpeech(ApplicationController &controller)
    {
        controller.providerRegistry()->registerSpeechProvider(
            {QStringLiteral("claude"), QStringLiteral("Scripted")},
            [this](QObject *parent) { return new ScriptedTranscriber(&m_script, parent); });
    }

    // The queue's choices for a batch that only transcribes.
    static TranscribeOptions queueOptions()
    {
        TranscribeOptions options = speechOnly();
        options.refinementProviderId = QStringLiteral("none");
        options.destination = TranscriptDestination::None;
        return options;
    }

    static TranscribeOptions speechOnly()
    {
        TranscribeOptions options;
        options.speechProviderId = QStringLiteral("claude");
        return options;
    }

    // A provider that streams finals as Codex does and, with connects,
    // connects each attempt at once; m_codex is the latest instance, the one
    // a session's attempts go to.
    void registerStreamingCodex(bool connects = true)
    {
        m_registry->registerSpeechProvider({QStringLiteral("codex"), QStringLiteral("ChatGPT Codex")},
                                           [this, connects](QObject *parent) {
                                               auto *codex = new FakeSpeechTranscriber(parent);
                                               codex->providerId = QStringLiteral("codex");
                                               codex->streamsFinals = true;
                                               if (connects) {
                                                   codex->onStartAttempt = [codex] { codex->emitConnected(); };
                                               }
                                               m_codex = codex;
                                               m_codexes << codex;
                                               return codex;
                                           });
    }

    // The providers that streamed, oldest first: a two-stream recording's
    // microphone, then its system audio.
    QList<FakeSpeechTranscriber *> streamingCodexes() const
    {
        QList<FakeSpeechTranscriber *> streaming;
        for (const QPointer<FakeSpeechTranscriber> &codex : m_codexes) {
            if (codex && codex->startCalls > 0) {
                streaming << codex;
            }
        }
        return streaming;
    }

    // The Custom Endpoint at server as settings' speech provider.
    void useEndpoint(SettingsStore &settings, const FakeServer &server)
    {
        m_registry->registerSpeechProvider({QStringLiteral("endpoint"), QStringLiteral("Custom Endpoint")},
                                           [](QObject *parent) { return new EndpointSpeechTranscriber(parent); });
        settings.edit([](AppSettings &draft) { draft.speech.providerId = QStringLiteral("endpoint"); return true; });
        settings.raw().setValue(SettingsKeys::SpeechEndpointBaseUrl, server.origin());
    }

    // Starts recording into path, or the default file in m_dir, and waits for
    // the start's outcome. Returns the file, or empty with error set.
    QString startRecording(RecordingSession &recording,
                           const QString &path,
                           QString *error,
                           const QStringList &vocabulary = {},
                           bool microphoneOnly = false)
    {
        std::optional<QString> outcome;
        recording.start(path, vocabulary, m_dir.path(), microphoneOnly,
                        [&outcome](const QString &failure) { outcome = failure; });
        if (!QTest::qWaitFor([&outcome] { return outcome.has_value(); }, 10000)) {
            *error = QStringLiteral("the start never finished");
            return {};
        }
        *error = *outcome;
        return error->isEmpty() ? recording.status().path : QString();
    }

    QTemporaryDir m_dir;
    std::unique_ptr<ProviderRegistry> m_registry;
    Script m_script;
    QPointer<FakeSpeechTranscriber> m_codex;
    // Every provider registerStreamingCodex made, oldest first.
    QList<QPointer<FakeSpeechTranscriber>> m_codexes;
    // Style and tone the last refinement ran with.
    QStringList m_refinedWith;
    QStringList m_refinedVocabulary;
    // The Spoken Language the last refinement was told.
    QString m_refinedLanguage;
};

} // namespace

int runFileTranscriptionTests(int argc, char **argv)
{
    FileTranscriptionTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_file_transcription.moc"
