#include "transcribe/FileTranscriptionSession.h"

#include "core/SettingsStore.h"
#include "core/TranscriptState.h"
#include "core/WritingProfileProviders.h"
#include "platform/audio/AudioPcmConverter.h"
#include "providers/ProviderRegistry.h"
#include "providers/SpeechAudio.h"
#include "transcribe/TranscribePresentation.h"

#include <QAudioDecoder>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMediaPlayer>
#include <QMimeDatabase>
#include <QSaveFile>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace speecher {
namespace {

using Stage = ProviderAttemptIssue::Stage;

// Waveform resolution handed to the front end.
constexpr int kPeakCount = 240;
// The most stretches PeakLevels keeps; enough that each of the front end's
// peaks spans several.
constexpr qsizetype kMostPeakStretches = qsizetype(16) * kPeakCount;
// How far the streaming pass reads ahead of what the speech provider was
// sent. Decoding runs thousands of times faster than the stream sends.
constexpr qsizetype kMostUnsentBytes = qsizetype(5) * kSpeechBytesPerSecond;

// A file's peak levels, taken as its 16 kHz mono audio decodes, so none of
// the audio is kept. Each stretch holds its loudest sample. Stretches start
// one sample long; once there are kMostPeakStretches, neighbours merge and
// later stretches are twice as long.
class PeakLevels {
public:
    void add(const QByteArray &pcm)
    {
        const auto *data = reinterpret_cast<const qint16 *>(pcm.constData());
        for (qsizetype i = 0; i < pcm.size() / 2; ++i) {
            if (m_samples++ % m_stretchSamples == 0) {
                if (m_stretches.size() == kMostPeakStretches) {
                    mergeStretches();
                }
                m_stretches.append(0.0f);
            }
            m_stretches.last() = std::max(m_stretches.last(), std::abs(data[i]) / 32768.0f);
        }
    }

    // kPeakCount levels (0..1) across the audio.
    QVector<float> levels() const
    {
        QVector<float> peaks(kPeakCount, 0.0f);
        for (qsizetype i = 0; i < m_stretches.size(); ++i) {
            const int bucket = int(qint64(i) * m_stretchSamples * kPeakCount / m_samples);
            peaks[bucket] = std::max(peaks[bucket], m_stretches.at(i));
        }
        return peaks;
    }

    qint64 bytes() const { return m_samples * 2; }

private:
    void mergeStretches()
    {
        for (qsizetype i = 0; i < m_stretches.size() / 2; ++i) {
            m_stretches[i] = std::max(m_stretches.at(2 * i), m_stretches.at(2 * i + 1));
        }
        m_stretches.resize(m_stretches.size() / 2);
        m_stretchSamples *= 2;
    }

    QVector<float> m_stretches;
    qint64 m_stretchSamples = 1;
    qint64 m_samples = 0;
};

} // namespace

QStringList transcribableExtensions()
{
    return {QStringLiteral("wav"), QStringLiteral("mp3"), QStringLiteral("m4a"), QStringLiteral("aac"),
            QStringLiteral("flac"), QStringLiteral("ogg"), QStringLiteral("oga"), QStringLiteral("opus"),
            QStringLiteral("webm"), QStringLiteral("mp4"), QStringLiteral("m4v"), QStringLiteral("mov"),
            QStringLiteral("mkv"), QStringLiteral("avi")};
}

bool isAudioFile(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isFile()) {
        return false;
    }
    const QString mime = QMimeDatabase().mimeTypeForFile(info).name();
    return mime.startsWith(QStringLiteral("audio/")) || mime.startsWith(QStringLiteral("video/"));
}

void probeAudioDuration(const QString &path, QObject *receiver, std::function<void(qint64)> done)
{
    auto *player = new QMediaPlayer(receiver);
    QObject::connect(player, &QMediaPlayer::mediaStatusChanged, receiver,
                     [player, done = std::move(done)](QMediaPlayer::MediaStatus status) {
                         if (status == QMediaPlayer::LoadedMedia && player->duration() > 0) {
                             done(player->duration());
                         }
                         if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::InvalidMedia) {
                             player->deleteLater();
                         }
                     });
    player->setSource(QUrl::fromLocalFile(path));
}

// Opened NewOnly, so an existing file is never overwritten, and a name taken
// by the time it opens moves on to the next. QSaveFile would be atomic, but
// its commit renames over whatever took the name meanwhile. A write that
// fails part way removes the file instead, so no truncated transcript is left
// to mistake for a whole one.
QString saveTranscript(const QString &audioPath,
                       const QString &folder,
                       const QString &text,
                       TranscriptFormat format,
                       QString *error)
{
    const QString stem = QFileInfo(audioPath).completeBaseName() + QStringLiteral("-transcribed");
    const QString extension = transcriptFileExtension(format);
    const QDir dir(folder);
    for (int copy = 1;; ++copy) {
        const QString name = copy == 1 ? QStringLiteral("%1.%2").arg(stem, extension)
                                       : QStringLiteral("%1 (%2).%3").arg(stem).arg(copy).arg(extension);
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            if (file.exists()) {
                continue;
            }
            *error = transcriptSaveError(file.fileName(), file.errorString());
            return {};
        }
        const QByteArray bytes = text.toUtf8() + '\n';
        if (file.write(bytes) != bytes.size() || !file.flush()) {
            *error = transcriptSaveError(file.fileName(), file.errorString());
            file.remove();
            return {};
        }
        return file.fileName();
    }
}

QString writeTranscript(const QString &path, const QString &text)
{
    QSaveFile file(path);
    const QByteArray bytes = text.toUtf8() + '\n';
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        return transcriptSaveError(path, file.errorString());
    }
    return {};
}

AppSettings withBatchChoices(AppSettings settings, const ProviderRegistry &providers, const TranscribeOptions &options)
{
    if (options.spokenLanguage) {
        settings.speech.language = *options.spokenLanguage;
    }
    // The page's providers start at the profile's. Kept, they run as the
    // profile has them, with its model and ahead of the saved chain; one the
    // page or command line changed runs as the settings have it.
    const AppSettings profiled = providers.withProfileProviders(
        settings, writingProfileSettingsFor(settings.refinement.writingProfiles,
                                            writingProfileFromName(options.writingProfile)));
    if (profiled.speech.providerId == options.speechProviderId) {
        settings.speech = profiled.speech;
    }
    if (profiled.refinement.providerId == options.refinementProviderId) {
        settings.refinement = profiled.refinement;
    }
    settings.speech.providerId = options.speechProviderId;
    settings.refinement.providerId = options.refinementProviderId;
    QString *speechModel = speechModelSetting(settings.speech, settings.speech.providerId);
    if (speechModel && !options.speechModel.isEmpty()) {
        *speechModel = options.speechModel;
    }
    if (!options.refinementModel.isEmpty()) {
        setRefinementModel(settings.refinement, options.refinementModel);
    }
    return settings;
}

FileTranscriptionSession::FileTranscriptionSession(SettingsStore *settings,
                                                   ProviderRegistry *providers,
                                                   QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_providers(providers)
    , m_transcript(new TranscriptState(this))
    , m_stream(new SpeechStream(providers, m_transcript, {.timedSegments = true}, this))
    , m_refinementChain(new RefinementChain(
          [this](const QString &providerId) { return acquireRefiner(providerId); }, this))
{
    qRegisterMetaType<TranscribeFileResult>();
    SpeechAttempts *speech = m_stream->attempts();
    // The provider starts once the worker reports, never inside start().
    speech->chain()->setOptions({.readyLater = true});
    connect(speech->chain(), &SpeechChain::secretsResolved, this,
            [this](const AppSettings &settings) { m_batchSettings = settings; });
    connect(m_stream, &SpeechStream::startedOver, this, [this] { m_current.segments.clear(); });
    connect(speech, &SpeechAttempts::segments, this, [this](const QList<TranscriptSegment> &segments) {
        for (TranscriptSegment segment : segments) {
            // A segment with no length or no words cannot be a cue.
            segment.text = segment.text.trimmed();
            if (segment.endMs <= segment.startMs || segment.text.isEmpty()) {
                continue;
            }
            segment.startMs += m_stream->attemptStartMs();
            segment.endMs += m_stream->attemptStartMs();
            m_current.segments.append(segment);
        }
    });
    connect(m_stream, &SpeechStream::progress, this, [this](qint64 bytesSent) {
        // A microphone's input has no length yet: its share is of what it
        // heard so far.
        const qint64 inputBytes = m_input ? bytesSent + m_stream->unsentBytes() : m_fileBytes;
        emit fileProgress(m_index, qreal(bytesSent) / qreal(inputBytes));
        readDecodedAudio();
    });
    connect(m_stream, &SpeechStream::finished, this, &FileTranscriptionSession::finishTranscription);
    connect(m_stream, &SpeechStream::failed, this, &FileTranscriptionSession::failFile);
    connect(m_refinementChain, &RefinementChain::passedOver, this,
            [this](const QString &providerId, Stage, const ProviderFailure &failure) {
                m_refinementFailures.append({providerId, failure.message});
            });
    connect(m_refinementChain, &RefinementChain::secretsResolved, this,
            [this](const AppSettings &settings) { m_batchSettings = settings; });
    connect(m_refinementChain, &RefinementChain::ready, this, &FileTranscriptionSession::startRefiner);
    connect(m_refinementChain, &RefinementChain::exhausted, this, &FileTranscriptionSession::completeUnrefined);
    connect(m_transcript, &TranscriptState::changed, this, [this](const QString &text) {
        if (m_running) {
            emit filePartialText(m_index, text);
        }
    });
}

FileTranscriptionSession::~FileTranscriptionSession()
{
    releaseFileResources();
}

bool FileTranscriptionSession::isRunning() const
{
    return m_running;
}

bool FileTranscriptionSession::start(const QStringList &paths, const TranscribeOptions &options)
{
    if (m_running || paths.isEmpty()) {
        return false;
    }
    m_input = nullptr;
    beginBatch(paths, options);
    return true;
}

bool FileTranscriptionSession::startListening(AudioInput *input, const TranscribeOptions &options)
{
    if (m_running) {
        return false;
    }
    m_input = input;
    // There is no file to save the transcript beside.
    TranscribeOptions listening = options;
    listening.destination = TranscriptDestination::None;
    beginBatch({QString()}, listening);
    return true;
}

// QtAudioInput::stop() spins an event loop for the post-roll; the audio it
// delivers meanwhile still counts, so the input only ends once it returns.
void FileTranscriptionSession::finishListening()
{
    if (m_microphone != Microphone::Listening) {
        return;
    }
    m_microphone = Microphone::Stopping;
    m_input->stop();
    m_microphone = Microphone::Off;
    m_stream->endInput();
}

void FileTranscriptionSession::beginBatch(const QStringList &paths, const TranscribeOptions &options)
{
    m_paths = paths;
    m_options = options;
    // Never waits for the keyring, so a caller's refusal check still holds
    // when the batch starts; the chains read keys on their worker.
    m_batchSettings = withBatchChoices(m_settings->dictationSnapshot(), *m_providers, options);
    // The page's profile stands in for the one a target would have implied,
    // for the terms that apply as for everything else.
    m_runChoices = {writingProfileFromName(options.writingProfile), options.addedVocabulary};
    if (!options.applyVocabulary) {
        m_batchSettings.vocabulary.clear();
        m_batchSettings.learnedCorrections.clear();
        m_batchSettings.bindings.clear();
    }
    m_batchSettings.speech.vocabulary = TranscriptPipeline::speechVocabulary(m_batchSettings, m_runChoices, Target{});
    m_results.clear();
    m_running = true;
    m_index = -1;
    finishFile();
}

void FileTranscriptionSession::cancel()
{
    if (!m_running) {
        return;
    }
    releaseFileResources();
    m_running = false;
    emit batchFinished(m_results, true);
}

void FileTranscriptionSession::startFile()
{
    m_current = {};
    m_current.path = m_paths.at(m_index);
    m_stream->reset();
    emit fileStarted(m_index, m_current.path);
    if (m_input) {
        startMicrophone();
        return;
    }

    measureFile();
}

// The first pass reads the whole file as fast as it decodes, for its length
// and waveform, and keeps none of its audio.
void FileTranscriptionSession::measureFile()
{
    auto peaks = std::make_shared<PeakLevels>();
    decodeFile([peaks](const QByteArray &pcm) { peaks->add(pcm); },
               [this, peaks] {
                   if (peaks->bytes() == 0) {
                       failFile(QStringLiteral("The file contains no audio"));
                       return;
                   }
                   m_fileBytes = peaks->bytes();
                   emit fileDecoded(m_index, peaks->levels(), m_fileBytes * 1000 / kSpeechBytesPerSecond);
                   streamFile();
               });
}

// The second pass decodes the file again into the stream, reading only while
// little of it waits to be sent, so a long file is never held whole. Decoding
// is deterministic, so this pass yields the m_fileBytes the first counted.
void FileTranscriptionSession::streamFile()
{
    decodeFile([this](const QByteArray &pcm) { m_stream->append(pcm); }, [this] { m_stream->endInput(); });
    m_stream->prepare(m_batchSettings);
}

// Starts a pass over the current file: takeAudio gets each buffer it
// decodes, and finished runs once all of them came.
void FileTranscriptionSession::decodeFile(std::function<void(const QByteArray &)> takeAudio,
                                          std::function<void()> finished)
{
    releaseDecoder();
    m_takeDecodedAudio = std::move(takeAudio);
    m_decodedFormat = {};
    m_decoder = new QAudioDecoder(this);
    connect(m_decoder, &QAudioDecoder::bufferReady, this, &FileTranscriptionSession::readDecodedAudio);
    connect(m_decoder, &QAudioDecoder::finished, this, std::move(finished));
    connect(m_decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), this, [this] {
        failFile(QStringLiteral("Could not decode the audio: %1").arg(m_decoder->errorString()));
    });
    m_decoder->setSource(QUrl::fromLocalFile(m_current.path));
    m_decoder->start();
}

// The decoder holds its next buffer until it is read, so leaving it unread
// pauses decoding; the stream's progress reads it once the stream has sent
// enough. The first pass never waits, as the stream is empty then.
void FileTranscriptionSession::readDecodedAudio()
{
    if (!m_decoder || !m_decoder->bufferAvailable() || m_stream->unsentBytes() >= kMostUnsentBytes) {
        return;
    }
    const QAudioBuffer buffer = m_decoder->read();
    if (buffer.format() != m_decodedFormat) {
        m_decodedFormat = buffer.format();
        m_converter.reset(m_decodedFormat);
    }
    const AudioPcmConversion converted = m_converter.convert(QByteArray(buffer.constData<char>(), buffer.byteCount()));
    if (!converted.error.isEmpty()) {
        failFile(QStringLiteral("Could not read the audio: %1").arg(converted.error));
        return;
    }
    m_takeDecodedAudio(converted.pcm16Mono16k);
}

// Signals from a retired decoder must not reach the next pass or file.
void FileTranscriptionSession::releaseDecoder()
{
    if (!m_decoder) {
        return;
    }
    disconnect(m_decoder, nullptr, this, nullptr);
    m_decoder->stop();
    m_decoder->deleteLater();
    m_decoder = nullptr;
}

// The microphone fills the stream from before the providers prepare.
void FileTranscriptionSession::startMicrophone()
{
    connect(m_input, &AudioInput::audioChunk, this, [this](const QByteArray &pcm) {
        if (m_microphone != Microphone::Off) {
            m_stream->append(pcm);
        }
    });
    // As a dictation: a microphone that drops after it heard something still
    // has those words transcribed.
    connect(m_input, &AudioInput::failed, this, [this](const QString &message) {
        if (!m_stream->hasAudio()) {
            failFile(message);
            return;
        }
        m_current.error = message;
        finishListening();
    });
    m_microphone = Microphone::Listening;
    QString error;
    if (!m_input->start(&error)) {
        m_microphone = Microphone::Off;
        failFile(error);
        return;
    }
    m_stream->prepare(m_batchSettings);
}

void FileTranscriptionSession::finishTranscription()
{
    const QString raw = m_transcript->text();
    if (raw.isEmpty()) {
        failFile(QStringLiteral("No speech was recognized"));
        return;
    }
    m_current.raw = raw;
    refine(raw);
}

void FileTranscriptionSession::refine(const QString &raw)
{
    m_pipeline = TranscriptPipeline::prepare(raw, m_batchSettings, m_runChoices, Target{},
                                             m_stream->speechSettings().language);
    // The page's choices stand in for what a target app would have implied.
    m_pipeline.refinementSettings.style = m_options.cleanupStrength;
    m_pipeline.refinementContext.tone = m_options.tone;
    TranscriptPipeline::resolveCustomChoices(m_pipeline);
    // Even a page or command line set to None translates for a profile with
    // an output language.
    m_pipeline.refinementSettings.style =
        refinedCleanupLevel(m_pipeline.refinementSettings.style, m_pipeline.refinementContext.outputLanguage);
    const RefinementSettings &refinement = m_pipeline.refinementSettings;
    if (m_options.refinementProviderId == kNoRefinementProvider || refinement.style == QStringLiteral("none")
        || m_pipeline.bindingResult.canSkipRefinement) {
        completeFile(m_pipeline.deliveryFallback);
        return;
    }
    // Keys and an expired sign-in load on the worker, as for a dictation:
    // never on this thread, where a cancel could run meanwhile. A cancel
    // drops them unanswered.
    m_refinementFailures.clear();
    emit fileRefining(m_index);
    m_refinementChain->begin(m_options.refinementProviderId, m_batchSettings.refinement.fallbackProviderIds);
    m_refinementChain->prepare(m_batchSettings, refinement);
}

// A fresh refiner for each one the file tries, never live dictation's.
TranscriptRefiner *FileTranscriptionSession::acquireRefiner(const QString &providerId)
{
    releaseRefiner();
    m_refiner = m_providers->createRefinementProvider(providerId, this);
    if (!m_refiner) {
        return nullptr;
    }
    connect(m_refiner, &TranscriptRefiner::completed, this, [this](const QString &text) {
        completeFile(TranscriptPipeline::restoreRefinedResult(m_pipeline, text).value_or(m_pipeline.deliveryFallback));
    });
    connect(m_refiner, &TranscriptRefiner::failed, this, &FileTranscriptionSession::handleRefinementFailure);
    return m_refiner;
}

void FileTranscriptionSession::releaseRefiner()
{
    if (!m_refiner) {
        return;
    }
    disconnect(m_refiner, nullptr, this, nullptr);
    m_refiner->cancel();
    m_refiner->deleteLater();
    m_refiner = nullptr;
}

void FileTranscriptionSession::startRefiner()
{
    m_refiner->refine(m_pipeline.refinementInput,
                      m_pipeline.refinementVocabulary,
                      m_pipeline.refinementContext,
                      m_refinementChain->currentSettings(),
                      m_batchSettings.accounts);
}

// A refiner that fails to answer for want of a service makes way for the
// next, as in a dictation; one that answers unusably or fails otherwise ends
// refinement.
void FileTranscriptionSession::handleRefinementFailure(const ProviderFailure &failure)
{
    m_refinementFailures.append({m_refinementChain->currentId(), failure.message});
    if (permitsProviderFallback(failure.kind)) {
        m_refinementChain->prepareNext(m_batchSettings, m_pipeline.refinementSettings);
        return;
    }
    completeUnrefined();
}

// No refiner refined the file: it keeps the text as heard, and says why for
// each refiner tried, by name once the file had fallbacks to try.
void FileTranscriptionSession::completeUnrefined()
{
    QStringList reasons;
    for (const auto &[providerId, message] : std::as_const(m_refinementFailures)) {
        reasons.append(m_refinementChain->providerIds().size() == 1
                           ? message
                           : QStringLiteral("%1: %2").arg(m_providers->refinementProviderLabel(providerId), message));
    }
    m_current.error = QStringLiteral("Refinement failed: %1").arg(reasons.join(QStringLiteral("; ")));
    completeFile(m_pipeline.deliveryFallback);
}

void FileTranscriptionSession::completeFile(const QString &text)
{
    m_current.refined = text;
    const QString folder = m_options.destination == TranscriptDestination::BesideInput
        ? QFileInfo(m_current.path).absolutePath()
        : m_options.folder;
    if (m_options.destination != TranscriptDestination::None) {
        QString error;
        m_current.savedPath = saveTranscript(m_current.path, folder, text, TranscriptFormat::Text, &error);
        if (!error.isEmpty()) {
            m_current.error = error;
        }
    }
    finishFile();
}

void FileTranscriptionSession::failFile(const QString &message)
{
    qWarning().noquote() << "file transcription failed path=" + m_current.path << "message=" + message;
    // The provider will never finish the partial now.
    m_stream->attempts()->commitPartial();
    m_current.raw = m_transcript->text();
    m_current.error = message;
    finishFile();
}

// Records the current file (if any) and moves on to the next, or ends the batch.
void FileTranscriptionSession::finishFile()
{
    releaseFileResources();
    if (m_index >= 0) {
        m_results.append(m_current);
        emit fileFinished(m_index, m_current);
    }
    if (++m_index < m_paths.size()) {
        startFile();
        return;
    }
    m_running = false;
    emit batchFinished(m_results, false);
}

void FileTranscriptionSession::releaseFileResources()
{
    m_stream->release();
    m_refinementChain->cancel();
    releaseDecoder();
    if (m_input) {
        disconnect(m_input, nullptr, this, nullptr);
        if (std::exchange(m_microphone, Microphone::Off) == Microphone::Listening) {
            m_input->stop();
        }
    }
    releaseRefiner();
}

} // namespace speecher
