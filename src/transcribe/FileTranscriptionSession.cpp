#include "transcribe/FileTranscriptionSession.h"

#include "core/SettingsStore.h"
#include "core/TranscriptState.h"
#include "core/WritingProfileProviders.h"
#include "core/settings/SpokenLanguages.h"
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

QVector<float> peakLevels(const QByteArray &pcm)
{
    const qsizetype samples = pcm.size() / 2;
    QVector<float> peaks(kPeakCount, 0.0f);
    if (samples == 0) {
        return peaks;
    }
    const auto *data = reinterpret_cast<const qint16 *>(pcm.constData());
    for (qsizetype i = 0; i < samples; ++i) {
        const int bucket = int(i * kPeakCount / samples);
        peaks[bucket] = std::max(peaks[bucket], std::abs(data[i]) / 32768.0f);
    }
    return peaks;
}

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

// Opened NewOnly, so an existing file is never overwritten even if one appears
// between the check and the write. QSaveFile would be atomic, but its commit
// renames over whatever took the name meanwhile. A write that fails part way
// removes the file instead, so no truncated transcript is left to mistake for
// a whole one.
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
        if (file.exists()) {
            continue;
        }
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
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
    , m_speechChain(new SpeechChain(
          providers, [this](const QString &providerId) { return acquireTranscriber(providerId); }, this))
    , m_refinementChain(new RefinementChain(
          [this](const QString &providerId) { return acquireRefiner(providerId); }, this))
    , m_transcript(new TranscriptState(this))
{
    qRegisterMetaType<TranscribeFileResult>();
    // The provider starts once the worker reports, never inside start().
    m_speechChain->setOptions({.readyLater = true});
    m_sendTimer.setInterval(kSpeechCatchUpIntervalMs);
    m_sendTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_sendTimer, &QTimer::timeout, this, &FileTranscriptionSession::sendNextChunk);
    connect(m_speechChain, &SpeechChain::passedOver, this,
            [this](const QString &providerId, Stage stage, const ProviderFailure &failure) {
                m_speechIssues.append({ProviderRole::Speech, providerId, stage, failure.kind, failure.message});
            });
    connect(m_speechChain, &SpeechChain::secretsResolved, this,
            [this](const AppSettings &settings) { m_batchSettings = settings; });
    connect(m_speechChain, &SpeechChain::ready, this, &FileTranscriptionSession::beginStreaming);
    connect(m_speechChain, &SpeechChain::exhausted, this,
            [this] { failFile(speechFailureText(m_speechIssues.value(0).message)); });
    connect(m_refinementChain, &RefinementChain::passedOver, this,
            [this](const QString &, Stage, const ProviderFailure &failure) { m_refinementFailure = failure.message; });
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
    if (m_streaming && !m_sendTimer.isActive()) {
        m_sendTimer.start();
    }
}

void FileTranscriptionSession::beginBatch(const QStringList &paths, const TranscribeOptions &options)
{
    m_paths = paths;
    m_options = options;
    m_batchSettings = withBatchChoices(m_settings->snapshot(), *m_providers, options);
    m_batchSettings.speech.timedSegments = true;
    // The page's profile stands in for the one a target would have implied,
    // for the terms that apply as for everything else.
    m_batchSettings.refinement.sessionWritingProfile = writingProfileFromName(options.writingProfile);
    if (!options.applyVocabulary) {
        m_batchSettings.vocabulary.clear();
        m_batchSettings.learnedCorrections.clear();
        m_batchSettings.bindings.clear();
    }
    m_batchSettings.sessionVocabulary = options.addedVocabulary;
    m_batchSettings.speech.vocabulary = TranscriptPipeline::speechVocabulary(m_batchSettings, Target{});
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
    m_transcript->clear();
    m_pcm.clear();
    m_pcmDropped = 0;
    m_inputFinished = false;
    emit fileStarted(m_index, m_current.path);
    if (m_input) {
        startMicrophone();
        return;
    }

    m_decoder = new QAudioDecoder(this);
    auto converter = std::make_shared<AudioPcmConverter>();
    auto sourceFormat = std::make_shared<QAudioFormat>();
    connect(m_decoder, &QAudioDecoder::bufferReady, this, [this, converter, sourceFormat] {
        const QAudioBuffer buffer = m_decoder->read();
        if (buffer.format() != *sourceFormat) {
            *sourceFormat = buffer.format();
            converter->reset(*sourceFormat);
        }
        const AudioPcmConversion converted =
            converter->convert(QByteArray(buffer.constData<char>(), buffer.byteCount()));
        if (!converted.error.isEmpty()) {
            m_decoder->stop();
            failFile(QStringLiteral("Could not read the audio: %1").arg(converted.error));
            return;
        }
        m_pcm += converted.pcm16Mono16k;
    });
    connect(m_decoder, &QAudioDecoder::finished, this, [this] {
        if (m_pcm.isEmpty()) {
            failFile(QStringLiteral("The file contains no audio"));
            return;
        }
        emit fileDecoded(m_index, peakLevels(m_pcm), m_pcm.size() * 1000 / kSpeechBytesPerSecond);
        prepareProviders();
    });
    connect(m_decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), this, [this] {
        failFile(QStringLiteral("Could not decode the audio: %1").arg(m_decoder->errorString()));
    });
    m_decoder->setSource(QUrl::fromLocalFile(m_current.path));
    m_decoder->start();
}

// The microphone fills m_pcm as a decoder would, from before the providers
// prepare, and the send timer keeps up with it once one is streaming.
void FileTranscriptionSession::startMicrophone()
{
    connect(m_input, &AudioInput::audioChunk, this, [this](const QByteArray &pcm) {
        if (m_microphone == Microphone::Off) {
            return;
        }
        m_pcm += pcm;
        if (m_streaming && !m_sendTimer.isActive()) {
            m_sendTimer.start();
        }
    });
    // As a dictation: a microphone that drops after it heard something still
    // has those words transcribed.
    connect(m_input, &AudioInput::failed, this, [this](const QString &message) {
        if (m_pcmDropped == 0 && m_pcm.isEmpty()) {
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
    prepareProviders();
}

// Each file walks the speech chain from the page's provider, as a
// dictation starts: the first that prepares takes the file. Once audio has
// gone to one, the file stays with it.
void FileTranscriptionSession::prepareProviders()
{
    m_speechIssues.clear();
    m_speechChain->begin(m_options.speechProviderId, m_batchSettings.speech.fallbackProviderIds);
    // The page's own provider fails the file, as before.
    if (const QString problem =
            spokenLanguageProblem(currentSpeechSettings(), m_providers->speechProviderLabel(m_speechChain->currentId()));
        !problem.isEmpty()) {
        failFile(problem);
        return;
    }
    m_speechChain->prepare(m_batchSettings);
}

// A fresh transcriber for each provider the file tries, never live
// dictation's.
SpeechTranscriber *FileTranscriptionSession::acquireTranscriber(const QString &providerId)
{
    releaseTranscriber();
    m_transcriber = m_providers->createSpeechProvider(providerId, this);
    if (m_transcriber) {
        connectTranscriber();
    }
    return m_transcriber;
}

// Why no speech provider could transcribe the file: with fallbacks, each one
// tried and why; otherwise the provider's own reason.
QString FileTranscriptionSession::speechFailureText(const QString &providerMessage) const
{
    if (m_speechChain->providerIds().size() == 1) {
        return providerMessage;
    }
    return noSpeechServiceText(m_speechIssues, [this](ProviderRole, const QString &id) {
        return m_providers->speechProviderLabel(id);
    });
}

void FileTranscriptionSession::connectTranscriber()
{
    connect(m_transcriber, &SpeechTranscriber::attemptConnected, this, [this](quint64 attemptId) {
        if (attemptId == m_attemptId) {
            m_providerConnected = true;
        }
    });
    connect(m_transcriber, &SpeechTranscriber::partialTranscript, this,
            [this](quint64 attemptId, const QString &text) {
                if (attemptId == m_attemptId) {
                    m_providerConnected = true;
                    m_transcript->setPartial(text);
                }
            });
    connect(m_transcriber, &SpeechTranscriber::finalTranscript, this,
            [this](quint64 attemptId, const QString &text) {
                if (attemptId == m_attemptId) {
                    m_providerConnected = true;
                    m_transcript->commitFinal(text);
                }
            });
    connect(m_transcriber, &SpeechTranscriber::attemptTranscript, this,
            [this](quint64 attemptId, const QString &text) {
                if (attemptId == m_attemptId) {
                    m_transcript->replaceAttemptFinals(text);
                }
            });
    connect(m_transcriber, &SpeechTranscriber::attemptSegments, this,
            [this](quint64 attemptId, const QList<TranscriptSegment> &segments) {
                if (attemptId != m_attemptId) {
                    return;
                }
                for (TranscriptSegment segment : segments) {
                    // A segment with no length or no words cannot be a cue.
                    segment.text = segment.text.trimmed();
                    if (segment.endMs <= segment.startMs || segment.text.isEmpty()) {
                        continue;
                    }
                    segment.startMs += m_attemptStartMs;
                    segment.endMs += m_attemptStartMs;
                    m_current.segments.append(segment);
                }
            });
    connect(m_transcriber, &SpeechTranscriber::attemptCompleted,
            this, &FileTranscriptionSession::handleAttemptCompleted);
    connect(m_transcriber, &SpeechTranscriber::failed,
            this, &FileTranscriptionSession::handleSpeechFailure);
}

// The provider takes the file from its start.
void FileTranscriptionSession::beginStreaming()
{
    m_transcript->clear();
    m_current.segments.clear();
    m_sent = m_pcmDropped;
    m_providerConnected = false;
    m_inputFinished = false;
    m_attempts.refill();
    m_attemptStartMs = 0;
    m_attempts.attemptStarted();
    const quint64 attemptId = ++m_attemptId;
    m_transcriber->startAttempt(attemptId, currentSpeechSettings());
    // A provider failing inside startAttempt() has made way for the next, or
    // failed the file: nothing may be sent for this attempt.
    if (attemptId != m_attemptId) {
        return;
    }
    m_streaming = true;
    m_sendTimer.start();
}

void FileTranscriptionSession::sendNextChunk()
{
    if (m_sent >= m_pcmDropped + m_pcm.size()) {
        m_sendTimer.stop();
        // The microphone's next chunk starts the timer again.
        if (m_microphone != Microphone::Off) {
            return;
        }
        m_inputFinished = true;
        m_transcriber->finishInput(m_attemptId);
        return;
    }
    const QByteArray chunk = m_pcm.mid(m_sent - m_pcmDropped, kSpeechChunkBytes);
    m_sent += chunk.size();
    const quint64 attemptId = m_attemptId;
    m_transcriber->sendAudio(attemptId, chunk);
    // A provider can fail the file from inside sendAudio().
    if (attemptId != m_attemptId) {
        return;
    }
    emit fileProgress(m_index, qreal(m_sent) / qreal(m_pcmDropped + m_pcm.size()));
    // Once a stream has connected, no other provider takes the input from its
    // start (see handleSpeechFailure) and its next stream picks up at the next
    // unsent chunk, so what it was sent is never needed again.
    if (m_input && m_providerConnected) {
        m_pcm.remove(0, m_sent - m_pcmDropped);
        m_pcmDropped = m_sent;
    }
}

// Mirrors DictationSession::startNextAttempt: a fresh stream on the same
// transcriber while the audio keeps flowing.
//
// Known limitation: audio the old stream received but had not transcribed yet
// is lost, leaving a silent gap in the transcript. The new stream picks up at
// the next unsent chunk because no provider reports how far into the audio its
// transcript reaches, so there is no position to rewind to; resending a guessed
// window would duplicate words instead.
void FileTranscriptionSession::startNextAttempt()
{
    commitPartial();
    const SpeechSettings speech = currentSpeechSettings();
    m_transcript->startAttempt(speech.language);
    m_attemptStartMs = m_sent * 1000 / kSpeechBytesPerSecond;
    m_attempts.attemptStarted();
    m_transcriber->startAttempt(++m_attemptId, speech);
}

SpeechSettings FileTranscriptionSession::currentSpeechSettings() const
{
    return chainSpeechSettings(m_batchSettings.speech, m_speechChain->currentId());
}

void FileTranscriptionSession::handleAttemptCompleted(quint64 attemptId)
{
    if (attemptId != m_attemptId) {
        return;
    }
    if (m_inputFinished) {
        finishTranscription();
        return;
    }
    // The provider ended a stream before the file did (a clean server close).
    if (!m_attempts.attemptWasStable()) {
        handleSpeechFailure(SpeechAttemptPolicy::endedTooSoon(attemptId));
        return;
    }
    m_attempts.refill();
    qInfo() << "file transcription stream ended by the provider; rolling over";
    startNextAttempt();
}

void FileTranscriptionSession::handleSpeechFailure(const SpeechFailure &failure)
{
    if (failure.attemptId != m_attemptId) {
        return;
    }
    // Nothing reached a service yet: no audio went out, or the file's first
    // attempt never connected and only buffered what it was given, and no
    // text came back. Then the next provider may take the file instead, from
    // its start, as the audio is on disk; after, never: what was sent is not
    // sent again.
    const bool nothingReachedAService = m_attemptStartMs == 0 && !m_providerConnected
        && (m_sent == 0 || failure.phase == SpeechPhase::Connect);
    const ProviderAttemptIssue issue{ProviderRole::Speech, m_speechChain->currentId(),
                                     nothingReachedAService ? Stage::Connect : Stage::Interrupted, failure.kind,
                                     failure.message};
    if (nothingReachedAService && m_speechChain->fallbackRemains(failure.kind)) {
        m_sendTimer.stop();
        m_speechIssues.append(issue);
        releaseTranscriber();
        m_speechChain->prepareNext(m_batchSettings);
        return;
    }
    m_attempts.refillIfAttemptWasStable();
    const bool reconnectable = !m_inputFinished && failure.retryable
        && failure.phase == SpeechPhase::Streaming;
    if (reconnectable && m_attempts.takeReconnect()) {
        qInfo().noquote() << "file transcription stream dropped, reconnecting: " + failure.message;
        startNextAttempt();
        return;
    }
    m_speechIssues.append(issue);
    // As a dictation: words the provider did return are the file's result.
    failFile(m_transcript->isEmpty() ? speechFailureText(failure.message) : failure.message);
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
    m_pipeline = TranscriptPipeline::prepare(raw, m_batchSettings, Target{}, currentSpeechSettings().language);
    // The page's choices stand in for what a target app would have implied.
    m_pipeline.refinementSettings.style = m_options.cleanupStrength;
    m_pipeline.refinementSettings.tone = m_options.tone;
    m_pipeline.refinementContext.tone = m_options.tone;
    m_pipeline.refinementContext.writingProfile = writingProfileFromName(m_options.writingProfile);
    TranscriptPipeline::fillUserInstructions(
        m_pipeline.refinementContext, m_pipeline.refinementSettings,
        writingProfileSettingsFor(m_pipeline.refinementSettings.writingProfiles,
                                  m_pipeline.refinementContext.writingProfile));
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
    m_refinementFailure.clear();
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
    m_refinementFailure = QStringLiteral("Refinement failed: %1").arg(failure.message);
    if (permitsProviderFallback(failure.kind)) {
        m_refinementChain->prepareNext(m_batchSettings, m_pipeline.refinementSettings);
        return;
    }
    completeUnrefined();
}

// No refiner refined the file: it keeps the text as heard, and says why.
void FileTranscriptionSession::completeUnrefined()
{
    m_current.error = m_refinementFailure;
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
    commitPartial();
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

// The id moves on first, so nothing the provider sends while it stops
// counts.
void FileTranscriptionSession::releaseTranscriber()
{
    if (!m_transcriber) {
        return;
    }
    disconnect(m_transcriber, nullptr, this, nullptr);
    m_streaming = false;
    m_transcriber->cancelAttempt(m_attemptId++);
    m_transcriber->deleteLater();
    m_transcriber = nullptr;
}

void FileTranscriptionSession::releaseFileResources()
{
    m_sendTimer.stop();
    m_speechChain->cancel();
    m_refinementChain->cancel();
    // Signals from a retired provider must not reach the next file.
    if (m_decoder) {
        disconnect(m_decoder, nullptr, this, nullptr);
        m_decoder->stop();
        m_decoder->deleteLater();
    }
    releaseTranscriber();
    if (m_input) {
        disconnect(m_input, nullptr, this, nullptr);
        if (std::exchange(m_microphone, Microphone::Off) == Microphone::Listening) {
            m_input->stop();
        }
    }
    releaseRefiner();
    m_pcm.clear();
    m_pcmDropped = 0;
}

void FileTranscriptionSession::commitPartial()
{
    const QString partial = m_transcript->partial();
    if (!partial.isEmpty()) {
        m_transcript->commitFinal(partial);
    }
}

} // namespace speecher
