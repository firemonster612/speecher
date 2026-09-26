#include "transcribe/FileTranscriptionSession.h"

#include "core/SettingsStore.h"
#include "core/TranscriptState.h"
#include "dictation/DictationSession.h"
#include "dictation/StartupPreparationRunner.h"
#include "platform/audio/AudioPcmConverter.h"
#include "providers/ProviderRegistry.h"

#include <QAudioDecoder>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <memory>

namespace speecher {
namespace {

constexpr int kBytesPerSecond = 16000 * 2;
// 100 ms of 16 kHz mono s16, the chunk size live capture sends.
constexpr qsizetype kChunkBytes = kBytesPerSecond / 10;
// One 100 ms chunk every 12 ms, about 8.3 times real time. Faster-than-real-time
// streaming is unproven for Claude Voice, which also caps what it buffers before
// its socket connects at 4 MB; at this rate even its 10 s connection timeout
// queues only about 83 s of audio (2.7 MB), so the cap is never reached.
constexpr int kSendIntervalMs = 12;
// Dropped streams reopened per file before it counts as failed; a stream that
// ran for a while refills the budget, as it does for dictation.
constexpr int kReconnectsPerFile = 2;
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

bool isAudioFile(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isFile()) {
        return false;
    }
    const QString mime = QMimeDatabase().mimeTypeForFile(info).name();
    return mime.startsWith(QStringLiteral("audio/")) || mime.startsWith(QStringLiteral("video/"));
}

// Opened NewOnly, so an existing file is never overwritten even if one appears
// between the check and the write. QSaveFile would be atomic, but its commit
// renames over whatever took the name meanwhile. A write that fails part way
// removes the file instead, so no truncated transcript is left to mistake for
// a whole one.
QString saveTranscript(const QString &audioPath, const QString &folder, const QString &text, QString *error)
{
    const QString stem = QFileInfo(audioPath).completeBaseName() + QStringLiteral("-transcribed");
    const QDir dir(folder);
    for (int copy = 1;; ++copy) {
        const QString name = copy == 1 ? stem + QStringLiteral(".txt")
                                       : QStringLiteral("%1 (%2).txt").arg(stem).arg(copy);
        QFile file(dir.filePath(name));
        if (file.exists()) {
            continue;
        }
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            *error = QStringLiteral("Could not save %1: %2").arg(file.fileName(), file.errorString());
            return {};
        }
        const QByteArray bytes = text.toUtf8() + '\n';
        if (file.write(bytes) != bytes.size() || !file.flush()) {
            *error = QStringLiteral("Could not save %1: %2").arg(file.fileName(), file.errorString());
            file.remove();
            return {};
        }
        return file.fileName();
    }
}

FileTranscriptionSession::FileTranscriptionSession(SettingsStore *settings,
                                                   ProviderRegistry *providers,
                                                   QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_providers(providers)
    , m_preparation(new StartupPreparationRunner(this))
    , m_transcript(new TranscriptState(this))
{
    qRegisterMetaType<TranscribeFileResult>();
    m_sendTimer.setInterval(kSendIntervalMs);
    m_sendTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_sendTimer, &QTimer::timeout, this, &FileTranscriptionSession::sendNextChunk);
    connect(m_preparation, &StartupPreparationRunner::completed, this,
            [this](const StartupPreparationResult &result) {
                if (result.generation != m_preparationGeneration) {
                    return;
                }
                if (!result.speech.ok) {
                    failFile(result.speech.message);
                    return;
                }
                beginStreaming();
            });
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
    m_paths = paths;
    m_options = options;
    m_batchSettings = m_settings->snapshot();
    m_batchSettings.speech.providerId = options.speechProviderId;
    m_batchSettings.refinement.providerId = options.refinementProviderId;
    if (!options.applyVocabulary) {
        m_batchSettings.speech.vocabulary.clear();
        m_batchSettings.learnedCorrections.clear();
        m_batchSettings.bindings.clear();
    }
    m_results.clear();
    m_running = true;
    m_index = -1;
    emit batchStarted(paths.size());
    finishFile();
    return true;
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
    m_sent = 0;
    m_inputFinished = false;
    emit fileStarted(m_index, m_current.path);

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
        emit fileDecoded(m_index, peakLevels(m_pcm), m_pcm.size() * 1000 / kBytesPerSecond);
        prepareProviders();
    });
    connect(m_decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), this, [this] {
        failFile(QStringLiteral("Could not decode the audio: %1").arg(m_decoder->errorString()));
    });
    m_decoder->setSource(QUrl::fromLocalFile(m_current.path));
    m_decoder->start();
}

void FileTranscriptionSession::prepareProviders()
{
    m_transcriber = m_providers->createSpeechProvider(m_options.speechProviderId, this);
    if (!m_transcriber) {
        failFile(QStringLiteral("Unknown speech provider: %1").arg(m_options.speechProviderId));
        return;
    }
    connect(m_transcriber, &SpeechTranscriber::partialTranscript, this,
            [this](quint64 attemptId, const QString &text) {
                if (attemptId == m_attemptId) {
                    m_transcript->setPartial(text);
                }
            });
    connect(m_transcriber, &SpeechTranscriber::finalTranscript, this,
            [this](quint64 attemptId, const QString &text) {
                if (attemptId == m_attemptId) {
                    m_transcript->commitFinal(text);
                }
            });
    connect(m_transcriber, &SpeechTranscriber::attemptTranscript, this,
            [this](quint64 attemptId, const QString &text) {
                if (attemptId == m_attemptId) {
                    m_transcript->replaceFinals(m_attemptBaseText.isEmpty()
                                                    ? text
                                                    : m_attemptBaseText + QLatin1Char(' ') + text);
                }
            });
    connect(m_transcriber, &SpeechTranscriber::attemptCompleted,
            this, &FileTranscriptionSession::handleAttemptCompleted);
    connect(m_transcriber, &SpeechTranscriber::failed,
            this, &FileTranscriptionSession::handleSpeechFailure);

    std::optional<RefinementRefreshJob> refreshJob;
    if (m_options.refinementProviderId != QStringLiteral("none")) {
        m_refiner = m_providers->createRefinementProvider(m_options.refinementProviderId, this);
        if (m_refiner) {
            refreshJob = m_refiner->createRefreshJob(m_batchSettings.refinement);
            if (!refreshJob && m_refiner->requiresRefresh(m_batchSettings.refinement)) {
                m_refiner->refresh(m_batchSettings.refinement);
            }
        }
    }
    // Credentials load off the UI thread when the provider offers a job for
    // it, exactly as a dictation starts.
    std::optional<SpeechPrepareJob> speechJob = m_transcriber->createPrepareJob(m_batchSettings.speech);
    SpeechPrepareResult prepared{true, {}};
    if (!speechJob) {
        prepared = m_transcriber->prepare(m_batchSettings.speech);
    }
    m_preparation->start(++m_preparationGeneration, std::move(speechJob), std::move(refreshJob), prepared);
}

void FileTranscriptionSession::beginStreaming()
{
    m_reconnectsLeft = kReconnectsPerFile;
    m_attemptBaseText.clear();
    m_attemptClock.start();
    m_transcriber->startAttempt(++m_attemptId, m_batchSettings.speech);
    m_sendTimer.start();
}

void FileTranscriptionSession::sendNextChunk()
{
    if (m_sent >= m_pcm.size()) {
        m_sendTimer.stop();
        m_inputFinished = true;
        m_transcriber->finishInput(m_attemptId);
        return;
    }
    const QByteArray chunk = m_pcm.mid(m_sent, kChunkBytes);
    m_sent += chunk.size();
    m_transcriber->sendAudio(m_attemptId, chunk);
    emit fileProgress(m_index, qreal(m_sent) / qreal(m_pcm.size()));
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
    const QString partial = m_transcript->partial();
    if (!partial.isEmpty()) {
        m_transcript->commitFinal(partial);
    }
    m_attemptBaseText = m_transcript->text();
    m_attemptClock.start();
    m_transcriber->startAttempt(++m_attemptId, m_batchSettings.speech);
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
    // The provider ended a stream before the file did (Codex sessions expire).
    if (!attemptWasStable()) {
        handleSpeechFailure({attemptId,
                             QStringLiteral("The speech stream ended within seconds of starting"),
                             true,
                             QStringLiteral("streaming")});
        return;
    }
    m_reconnectsLeft = kReconnectsPerFile;
    qInfo() << "file transcription stream ended by the provider; rolling over";
    startNextAttempt();
}

void FileTranscriptionSession::handleSpeechFailure(const SpeechFailure &failure)
{
    if (failure.attemptId != m_attemptId) {
        return;
    }
    if (attemptWasStable()) {
        m_reconnectsLeft = kReconnectsPerFile;
    }
    const bool reconnectable = !m_inputFinished && failure.retryable
        && failure.phase == QStringLiteral("streaming");
    if (reconnectable && m_reconnectsLeft > 0) {
        --m_reconnectsLeft;
        qInfo().noquote() << "file transcription stream dropped, reconnecting: " + failure.message;
        startNextAttempt();
        return;
    }
    failFile(failure.message);
}

bool FileTranscriptionSession::attemptWasStable() const
{
    return m_attemptClock.isValid() && m_attemptClock.elapsed() >= DictationSession::stableAttemptMs();
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
    m_pipeline = TranscriptPipeline::prepare(raw, m_batchSettings, Target{});
    // The page's choices stand in for what a target app would have implied.
    m_pipeline.refinementSettings.style = m_options.cleanupStrength;
    m_pipeline.refinementSettings.tone = m_options.tone;
    m_pipeline.refinementContext.tone = m_options.tone;
    m_pipeline.refinementContext.writingProfile = writingProfileFromName(m_options.writingProfile);
    const RefinementSettings &refinement = m_pipeline.refinementSettings;
    if (!m_refiner || refinement.style == QStringLiteral("none")
        || m_pipeline.bindingResult.canSkipRefinement) {
        completeFile(m_pipeline.deliveryFallback);
        return;
    }
    const RefinementPrepareResult prepared = m_refiner->prepare(refinement);
    if (!prepared.ok) {
        m_current.error = prepared.message;
        completeFile(m_pipeline.deliveryFallback);
        return;
    }
    connect(m_refiner, &TranscriptRefiner::completed, this, [this](const QString &text) {
        completeFile(TranscriptPipeline::restoreRefinedResult(m_pipeline, text)
                         .value_or(m_pipeline.deliveryFallback));
    });
    connect(m_refiner, &TranscriptRefiner::failed, this, [this](const QString &message) {
        m_current.error = QStringLiteral("Refinement failed: %1").arg(message);
        completeFile(m_pipeline.deliveryFallback);
    });
    emit fileRefining(m_index);
    m_refiner->refine(m_pipeline.refinementInput,
                      m_pipeline.refinementVocabulary,
                      m_pipeline.refinementContext,
                      refinement);
}

void FileTranscriptionSession::completeFile(const QString &text)
{
    m_current.refined = text;
    const QString folder = m_options.destination == TranscriptDestination::BesideInput
        ? QFileInfo(m_current.path).absolutePath()
        : m_options.folder;
    if (m_options.destination != TranscriptDestination::None) {
        QString error;
        m_current.savedPath = saveTranscript(m_current.path, folder, text, &error);
        if (!error.isEmpty()) {
            m_current.error = error;
        }
    }
    finishFile();
}

void FileTranscriptionSession::failFile(const QString &message)
{
    qWarning().noquote() << "file transcription failed path=" + m_current.path << "message=" + message;
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
    m_sendTimer.stop();
    m_preparation->cancel();
    ++m_preparationGeneration;
    // Signals from a retired provider must not reach the next file.
    if (m_decoder) {
        disconnect(m_decoder, nullptr, this, nullptr);
        m_decoder->stop();
        m_decoder->deleteLater();
    }
    if (m_transcriber) {
        disconnect(m_transcriber, nullptr, this, nullptr);
        m_transcriber->cancelAttempt(m_attemptId);
        m_transcriber->deleteLater();
    }
    if (m_refiner) {
        disconnect(m_refiner, nullptr, this, nullptr);
        m_refiner->cancel();
        m_refiner->deleteLater();
    }
    m_pcm.clear();
}

} // namespace speecher
