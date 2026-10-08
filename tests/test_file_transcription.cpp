#include "common/test_suites.h"
#include "common/test_doubles.h"

#include "app/HeadlessTranscribe.h"
#include "core/SettingsStore.h"
#include "core/VocabularyLimit.h"
#include "dictation/DictationSession.h"
#include "transcribe/FileTranscriptionSession.h"
#include "transcribe/Subtitles.h"
#include "transcribe/TranscribePresentation.h"

#include <QDir>
#include <QElapsedTimer>
#include <QMediaFormat>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QFile>
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
#include <QAudioBufferInput>
#include <QImage>
#include <QMediaCaptureSession>
#include <QMediaRecorder>
#include <QVideoFrame>
#include <QVideoFrameInput>
#endif
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

#include <cmath>
#include <csignal>
#include <sstream>
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
    int attempts = 0;
    qsizetype bytes = 0;
    QStringList vocabulary;
    bool timedSegments = false;
    // Sent with each finished input, timed from the start of its attempt.
    QList<TranscriptSegment> segments;
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
        m_script->timedSegments = settings.timedSegments;
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
        if (!m_script->segments.isEmpty()) {
            emit attemptSegments(attemptId, m_script->segments);
        }
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

QString readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
}

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
        m_registry->registerRefinementProvider({QStringLiteral("openai"), QStringLiteral("Fake")},
                                               [this](QObject *parent) {
                                                   auto *refiner = new FakeRefiner(parent);
                                                   connect(refiner, &TranscriptRefiner::completed, this, [this, refiner] {
                                                       m_refinedWith = {refiner->lastStyle, refiner->lastTone};
                                                       m_refinedVocabulary = refiner->lastVocabulary;
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

    // Transcribe asks for timings, and keeps the segments that can be cues.
    // The first stream ends after one 100 ms chunk, so the second attempt's
    // audio starts 100 ms into the file.
    void timesUsableSegmentsFromTheStartOfTheFile()
    {
        DictationSession::setStableAttemptMs(0);
        const auto restore = qScopeGuard([] { DictationSession::setStableAttemptMs(10000); });
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
        QCOMPARE(results.first().segments, QList<TranscriptSegment>({{100, 300, QStringLiteral("heard")}}));
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

    // Refinement is the reason given only when the batch refined.
    void subtitlesNoteGivesTheReasonThatApplies()
    {
        TranscribeFileResult timed;
        timed.raw = timed.refined = QStringLiteral("heard");
        timed.segments = {{0, 1000, QStringLiteral("heard")}};
        TranscribeBatchLabels labels{QStringLiteral("Local Model"), {}};
        const QString unrefined = subtitlesNote({timed}, labels);
        QVERIFY(unrefined.contains(QStringLiteral("Raw Transcript")));
        QVERIFY(!unrefined.contains(QStringLiteral("refinement")));

        labels.refinement = QStringLiteral("OpenAI");
        QVERIFY(subtitlesNote({timed}, labels).contains(QStringLiteral("refinement")));

        timed.segments.clear();
        QVERIFY(subtitlesNote({timed}, labels).contains(QStringLiteral("Local Model")));
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
                                                                           QStringLiteral("connect"),
                                                                           ProviderFailureKind::Network);
                                                   };
                                               } else {
                                                   speech->prepareResult = {false, QStringLiteral("offline"),
                                                                            ProviderFailureKind::Network};
                                               }
                                               codex.append(speech);
                                               return speech;
                                           });
        const QString first = m_dir.filePath(QStringLiteral("first.wav"));
        const QString second = m_dir.filePath(QStringLiteral("second.wav"));
        writeWav(first);
        writeWav(second);
        SettingsStore settings;
        settings.setSpeechProvider(QStringLiteral("endpoint"));
        settings.setSpeechFallbackProviders({QStringLiteral("codex"), QStringLiteral("claude")});
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
        settings.setSpeechProvider(QStringLiteral("codex"));
        settings.setSpeechFallbackProviders({QStringLiteral("claude")});
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        TranscribeOptions options;
        options.speechProviderId = QStringLiteral("codex");

        QVERIFY(session.start({audio}, options));
        QTRY_VERIFY_WITH_TIMEOUT(codex && !codex->audioChunks.isEmpty(), 10000);
        codex->emitFailure(QStringLiteral("dropped"), false, QStringLiteral("streaming"), ProviderFailureKind::Network);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);

        const auto results = finished.first().first().value<QList<TranscribeFileResult>>();
        QCOMPARE(results.first().error, QStringLiteral("No speech service is available. ChatGPT Codex dropped."));
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
        settings.setSpeechProvider(QStringLiteral("codex"));
        settings.setSpeechFallbackProviders({QStringLiteral("claude")});
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        TranscribeOptions options;
        options.speechProviderId = QStringLiteral("codex");

        QVERIFY(session.start({audio}, options));
        QTRY_VERIFY_WITH_TIMEOUT(codex && !codex->audioChunks.isEmpty(), 10000);
        codex->emitFailure(QStringLiteral("offline"), false, QStringLiteral("connect"), ProviderFailureKind::Network);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);

        const TranscribeFileResult result = finished.first().first().value<QList<TranscribeFileResult>>().first();
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(m_script.attempts, 1);
        QVERIFY(qAbs(m_script.bytes - 16000) <= 8);
        QCOMPARE(result.raw, QStringLiteral("heard %1").arg(m_script.bytes));
    }

    // The refiner's sign-in renews once per file, not again for each speech
    // provider tried.
    void theRefinerRenewsOncePerFile()
    {
        FakeRefiner *refiner = nullptr;
        m_registry->registerRefinementProvider({QStringLiteral("openai"), QStringLiteral("Fake")},
                                               [&refiner](QObject *parent) {
                                                   refiner = new FakeRefiner(parent);
                                                   refiner->refreshRequired = true;
                                                   return refiner;
                                               });
        m_registry->registerSpeechProvider({QStringLiteral("codex"), QStringLiteral("ChatGPT Codex")},
                                           [](QObject *parent) {
                                               auto *speech = new FakeSpeechTranscriber(parent);
                                               speech->prepareResult = {false, QStringLiteral("offline"),
                                                                        ProviderFailureKind::Network};
                                               return speech;
                                           });
        const QString audio = m_dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        SettingsStore settings;
        settings.setSpeechProvider(QStringLiteral("codex"));
        settings.setSpeechFallbackProviders({QStringLiteral("claude")});
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        TranscribeOptions options;
        options.speechProviderId = QStringLiteral("codex");
        options.refinementProviderId = QStringLiteral("openai");
        // Read while the file's refiner still exists: it goes once the file is done.
        int refreshes = -1;
        connect(&session, &FileTranscriptionSession::fileFinished, this,
                [&refiner, &refreshes] { refreshes = refiner->refreshCalls; });

        QVERIFY(session.start({audio}, options));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        QCOMPARE(refreshes, 1);
    }

    // The last provider failing once it started is named with the others.
    void theLastProviderFailingAfterItStartedIsNamed()
    {
        const auto failingInsideStartAttempt = [](const QString &message, ProviderFailureKind kind) {
            return [message, kind](QObject *parent) {
                auto *speech = new FakeSpeechTranscriber(parent);
                speech->onStartAttempt = [speech, message, kind] {
                    speech->emitFailure(message, false, QStringLiteral("connect"), kind);
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
        settings.setSpeechProvider(QStringLiteral("codex"));
        settings.setSpeechFallbackProviders({QStringLiteral("endpoint")});
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
        settings.setSpeechProvider(QStringLiteral("codex"));
        settings.setSpeechFallbackProviders({QStringLiteral("claude")});
        FileTranscriptionSession session(&settings, m_registry.get());
        QSignalSpy finished(&session, &FileTranscriptionSession::batchFinished);
        FakeAudioInput microphone;
        microphone.onStart = [&microphone] { microphone.pushAudio(microphoneChunk(8000)); };
        TranscribeOptions options;
        options.speechProviderId = QStringLiteral("codex");

        QVERIFY(session.startListening(&microphone, options));
        QVERIFY(microphone.active);
        QTRY_VERIFY_WITH_TIMEOUT(codex && !codex->audioChunks.isEmpty(), 10000);
        microphone.pushAudio(microphoneChunk(8000));
        codex->emitFailure(QStringLiteral("offline"), false, QStringLiteral("connect"), ProviderFailureKind::Network);
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
        microphone.onStart = [&microphone] {
            for (int i = 0; i < 5; ++i) {
                QTimer::singleShot(i * 100, &microphone, [&microphone] { microphone.pushAudio(microphoneChunk(0)); });
            }
            QTimer::singleShot(500, &microphone, [&microphone] { microphone.pushAudio(microphoneChunk(8000)); });
        };
        HeadlessTranscribeOptions options;
        options.speechProviderId = QStringLiteral("claude");
        options.refinementProviderId = QStringLiteral("none");
        std::ostringstream out;
        std::ostringstream err;
        // A run the silence never ends stops here, and fails the timing below.
        QElapsedTimer clock;
        clock.start();

        QCOMPARE(runHeadlessListen(options, 300, &microphone, [&clock] { return clock.elapsed() > 3000; }, false,
                                   &settings, m_registry.get(), out, err),
                 0);
        QVERIFY2(clock.elapsed() < 2500, qPrintable(QString::number(clock.elapsed())));
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

    // A term only the run adds reaches the speech provider ahead of the saved
    // ones, and refinement, while the saved vocabulary stays as it was.
    void addedVocabularyIsForThisBatchOnly()
    {
        const QString audio = m_dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        SettingsStore settings;
        settings.setVocabularyEntries({{QStringLiteral("Speecher")}});
        const QList<VocabularyEntry> saved = settings.vocabularyEntries();
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
        QCOMPARE(settings.vocabularyEntries(), saved);
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
        settings.setVocabularyEntries({favourite});
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
        settings.setVocabularyEntries({longer, shorter});
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
        settings.setVocabularyEntries({{QStringLiteral("Speecher")}, elsewhere});
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
        std::ostringstream out;
        std::ostringstream err;
        const auto text = [&out] {
            const QString first = QString::fromStdString(out.str()).section(QLatin1Char('\n'), 0, 0);
            return QJsonDocument::fromJson(first.toUtf8()).object().value(QStringLiteral("text")).toString();
        };

        QCOMPARE(runHeadlessTranscribe({audio}, options, &settings, m_registry.get(), out, err, false), 0);
        QCOMPARE(text(), QStringLiteral("Anthropic heard it."));
        QCOMPARE(anthropicModel, QStringLiteral("claude-sonnet-5-5"));

        options.refinementProviderId = QStringLiteral("openai");
        out.str({});
        QCOMPARE(runHeadlessTranscribe({audio}, options, &settings, m_registry.get(), out, err, false), 0);
        QCOMPARE(text(), QStringLiteral("Heard it."));

        // A profile's speech service this build lacks is passed over for the
        // settings' one, not refused as an unknown --model.
        stored = settings.snapshot();
        stored.refinement.writingProfiles[1].speechProvider = QStringLiteral("local");
        settings.applySnapshot(stored);
        out.str({});
        QCOMPARE(runHeadlessTranscribe({audio}, options, &settings, m_registry.get(), out, err, false), 0);
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
    }

    // `transcribe -` names the piped audio stdin, fails on empty stdin, and
    // removes the spooled audio either way.
    void headlessRunReadsAudioFromStdin()
    {
        QTemporaryDir dir;
        QTemporaryDir spool;
        const QByteArray savedTmpdir = qgetenv("TMPDIR");
        qputenv("TMPDIR", QFile::encodeName(spool.path()));
        const auto restoreTmpdir = qScopeGuard([&] { qputenv("TMPDIR", savedTmpdir); });
        const QString audio = dir.filePath(QStringLiteral("memo.wav"));
        writeWav(audio);
        QFile wav(audio);
        QVERIFY(wav.open(QIODevice::ReadOnly));
        std::istringstream in(wav.readAll().toStdString());
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
        QVERIFY(QDir(spool.path()).isEmpty());

        std::istringstream empty;
        out.str({});
        err.str({});
        QCOMPARE(runHeadlessTranscribe({kStdinFile}, options, &settings, m_registry.get(), empty, out, err, false), 1);
        QCOMPARE(QString::fromStdString(err.str()), QStringLiteral("stdin: Reading the audio\u2026\nNo audio on stdin\n"));
        const QJsonObject summary = QJsonDocument::fromJson(QByteArray::fromStdString(out.str())).object();
        QCOMPARE(summary.value(QStringLiteral("failed")).toInt(), 1);
        QCOMPARE(summary.value(QStringLiteral("succeeded")).toInt(), 0);
        QVERIFY(QDir(spool.path()).isEmpty());
    }

#ifdef Q_OS_UNIX
    // A disk that fills while stdin is spooled fails the run, rather than
    // transcribing what fit. A file size limit stands in for the full disk.
    void headlessRunFailsWhenStdinDoesNotFit()
    {
        QTemporaryDir spool;
        const QByteArray savedTmpdir = qgetenv("TMPDIR");
        qputenv("TMPDIR", QFile::encodeName(spool.path()));
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
            qputenv("TMPDIR", savedTmpdir);
        });
        // Fills the limit, then leaves a tail that only the last flush writes.
        std::istringstream in(std::string(kLimit + 100, 'a'));
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
        // Each extension's type is one the .desktop file registers, under its
        // own name, an alias or a type it inherits: the databases disagree on
        // which name is canonical (the one Qt bundles for macOS and Windows
        // calls .aac audio/x-aac, shared-mime-info calls it audio/aac).
        const QMimeDatabase mimes;
        for (const QString &extension : extensions) {
            const QMimeType type = mimes.mimeTypeForFile(QStringLiteral("x.") + extension, QMimeDatabase::MatchExtension);
            const QStringList listed = transcribableMimeTypes();
            const bool registered = std::any_of(listed.cbegin(), listed.cend(), [&](const QString &name) {
                const QMimeType known = mimes.mimeTypeForName(name);
                return known.isValid() && (type == known || type.inherits(known.name()));
            });
            QVERIFY2(registered, qPrintable(extension + QStringLiteral(" is ") + type.name()));
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
    QStringList m_refinedVocabulary;
};

} // namespace

int runFileTranscriptionTests(int argc, char **argv)
{
    FileTranscriptionTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_file_transcription.moc"
