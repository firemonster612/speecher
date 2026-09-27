#include "common/test_suites.h"
#include "common/test_doubles.h"

#include "app/HeadlessTranscribe.h"
#include "core/SettingsStore.h"
#include "dictation/DictationSession.h"
#include "transcribe/FileTranscriptionSession.h"
#include "transcribe/TranscribePresentation.h"

#include <QAudioBufferInput>
#include <QDir>
#include <QImage>
#include <QMediaCaptureSession>
#include <QMediaFormat>
#include <QMediaRecorder>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QFile>
#include <QVideoFrame>
#include <QVideoFrameInput>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

#include <cmath>
#include <sstream>

using namespace speecher;
using namespace speecher::test;

namespace {

// What the scripted transcribers saw. Owned by the test, because the session
// deletes each file's transcriber once the file is done.
struct Script {
    bool expireFirstStream = false;
    int attempts = 0;
    qsizetype bytes = 0;
    QStringList vocabulary;
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

    QString id() const override { return QStringLiteral("claude"); }
    QString label() const override { return QStringLiteral("Scripted"); }
    bool requiresRefresh(const SpeechSettings &) const override { return false; }
    SpeechPrepareResult prepare(const SpeechSettings &) override { return {true, {}}; }

    void startAttempt(quint64, const SpeechSettings &settings) override
    {
        ++m_script->attempts;
        m_script->vocabulary = settings.vocabulary;
        m_bytes = 0;
    }

    void sendAudio(quint64 attemptId, const QByteArray &pcm) override
    {
        m_script->bytes += pcm.size();
        m_bytes += pcm.size();
        if (m_script->expireFirstStream && m_script->attempts == 1) {
            emit finalTranscript(attemptId, QStringLiteral("before"));
            emit attemptCompleted(attemptId);
        }
    }

    void finishInput(quint64 attemptId) override
    {
        emit finalTranscript(attemptId, QStringLiteral("heard %1").arg(m_bytes));
        emit attemptCompleted(attemptId);
    }

    void cancelAttempt(quint64) override {}

private:
    Script *m_script;
    qsizetype m_bytes = 0;
};

// Half a second of a 440 Hz tone as 44.1 kHz stereo s16, so the file has to
// be downmixed and resampled on its way to the provider.
void writeWav(const QString &path)
{
    constexpr int rate = 44100;
    constexpr int channels = 2;
    QByteArray data;
    for (int i = 0; i < rate / 2; ++i) {
        const auto sample = qint16(8000 * std::sin(2 * M_PI * 440 * i / rate));
        for (int c = 0; c < channels; ++c) {
            data.append(reinterpret_cast<const char *>(&sample), 2);
        }
    }
    const auto le32 = [](quint32 v) { QByteArray b(4, 0); qToLittleEndian(v, b.data()); return b; };
    const auto le16 = [](quint16 v) { QByteArray b(2, 0); qToLittleEndian(v, b.data()); return b; };
    QByteArray wav = "RIFF" + le32(36 + data.size()) + "WAVEfmt " + le32(16) + le16(1) + le16(channels)
        + le32(rate) + le32(rate * channels * 2) + le16(channels * 2) + le16(16) + "data"
        + le32(data.size()) + data;
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(wav);
}

QString readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
}

// Records half a second of 440 Hz tone beside a few plain video frames into
// path with Qt's own FFmpeg recorder, so the test needs no fixture files.
// Returns why it could not, or empty once the file is written.
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

// Every first capture of pattern in a file under packaging/, sorted.
QStringList packagingEntries(const QString &file, const QString &pattern)
{
    QFile source(QStringLiteral(SPEECHER_SOURCE_DIR "/packaging/") + file);
    if (!source.open(QIODevice::ReadOnly)) {
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
        m_registry->registerRefinementProvider({QStringLiteral("openai"), QStringLiteral("Fake")},
                                               [this](QObject *parent) {
                                                   auto *refiner = new FakeRefiner(parent);
                                                   connect(refiner, &TranscriptRefiner::completed, this, [this, refiner] {
                                                       m_refinedWith = {refiner->lastStyle, refiner->lastTone};
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

    void rollsOverAStreamThatEndsMidFile()
    {
        DictationSession::setStableAttemptMs(0);
        const auto restore = qScopeGuard([] { DictationSession::setStableAttemptMs(10000); });
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

    void withoutVocabularyTheProviderHearsNone()
    {
        const QString audio = m_dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        SettingsStore settings;
        settings.setVocabularyEntries({{QStringLiteral("Speecher")}});
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
        std::ostringstream out;
        std::ostringstream err;

        QCOMPARE(runHeadlessTranscribe({audio}, options, &settings, m_registry.get(), out, err, false), 0);
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
        QCOMPARE(runHeadlessTranscribe({broken, audio}, options, &settings, m_registry.get(), out, err, false), 1);
        const QString printed = QString::fromStdString(out.str());
        QVERIFY2(printed.startsWith(QStringLiteral("# memo.wav\n\nheard ")), qPrintable(printed));
        QVERIFY(!printed.contains(QStringLiteral("broken.wav")));
        QVERIFY(QString::fromStdString(err.str()).contains(QStringLiteral("broken.wav: failed: ")));
        QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("memo-transcribed (2).txt"))));
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
        std::ostringstream out;
        std::ostringstream err;

        QCOMPARE(runHeadlessTranscribe({audio}, options, &settings, m_registry.get(), out, err, false), 1);
        QStringList lines = QString::fromStdString(out.str()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 2);
        const QJsonObject result = QJsonDocument::fromJson(lines.at(0).toUtf8()).object();
        QCOMPARE(result.value(QStringLiteral("ok")).toBool(), false);
        QVERIFY(result.value(QStringLiteral("error")).toString().startsWith(QStringLiteral("Could not save")));
        QCOMPARE(QJsonDocument::fromJson(lines.at(1).toUtf8()).object().value(QStringLiteral("failed")).toInt(), 1);
        QVERIFY(QString::fromStdString(err.str()).contains(QStringLiteral("memo.wav: failed: Could not save")));

        out.str({});
        err.str({});
        QCOMPARE(runHeadlessTranscribe({}, options, &settings, m_registry.get(), out, err, false), 2);
        QVERIFY(!err.str().empty());
        QVERIFY(QJsonDocument::fromJson(QByteArray::fromStdString(out.str())).object().value(QStringLiteral("summary")).toBool());
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
        QTest::newRow("mkv") << QStringLiteral("mkv") << QMediaFormat::Matroska << A::AAC << V::MPEG4;
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
        const QString clip = m_dir.filePath(QStringLiteral("clip.") + extension);
        const QString recordError = recordClip(clip, container, audioCodec, videoCodec);
        if (!recordError.isEmpty()) {
            QSKIP(qPrintable(QStringLiteral("Qt cannot write this clip here: ") + recordError));
        }
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
    }

    // "Open with" offers Speecher for exactly the files its pickers take.
    void packagingRegistersTheTranscribableTypes()
    {
        const QStringList extensions = sorted(transcribableExtensions());
        const QStringList desktop = packagingEntries(QStringLiteral("io.github.firemonster612.speecher.desktop"),
                                                     QStringLiteral("^MimeType=(.*)$"));
        QCOMPARE(desktop.size(), 1);
        QCOMPARE(sorted(desktop.first().split(u';', Qt::SkipEmptyParts)), sorted(transcribableMimeTypes()));
        QCOMPARE(packagingEntries(QStringLiteral("macos/Info.plist.in"),
                                  QStringLiteral("^\\t\\t\\t\\t<string>(\\w+)</string>$")),
                 extensions);
        QCOMPARE(packagingEntries(QStringLiteral("windows/speecher.iss"),
                                  QStringLiteral("Classes\\\\\\.(\\w+)\\\\OpenWithProgids")),
                 extensions);
        QCOMPARE(packagingEntries(QStringLiteral("windows/speecher.iss"),
                                  QStringLiteral("SupportedTypes\"; ValueType: string; ValueName: \"\\.(\\w+)\"")),
                 extensions);
        // Each extension's own type is one the .desktop file registers.
        for (const QString &extension : extensions) {
            const QString type = QMimeDatabase()
                                     .mimeTypeForFile(QStringLiteral("x.") + extension, QMimeDatabase::MatchExtension)
                                     .name();
            QVERIFY2(transcribableMimeTypes().contains(type), qPrintable(extension + QStringLiteral(" is ") + type));
        }
    }

private:
    static TranscribeOptions speechOnly()
    {
        TranscribeOptions options;
        options.speechProviderId = QStringLiteral("claude");
        return options;
    }

    QTemporaryDir m_dir;
    std::unique_ptr<ProviderRegistry> m_registry;
    Script m_script;
    // Style and tone the last refinement ran with.
    QStringList m_refinedWith;
};

} // namespace

int runFileTranscriptionTests(int argc, char **argv)
{
    FileTranscriptionTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_file_transcription.moc"
