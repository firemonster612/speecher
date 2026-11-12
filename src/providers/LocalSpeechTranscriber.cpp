#include "providers/LocalSpeechTranscriber.h"

#include "core/LocalModelCatalog.h"
#include "core/WordSpacing.h"
#include "providers/LocalModelStore.h"
#include "providers/LocalSpeechWorker.h"

#include <QMutex>
#include <QtEndian>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <utility>

namespace speecher {
namespace {

// How long an utterance may wait for its text once all of it is in: the
// worker runs one call at a time, so it may first finish the other side of a
// call, and a batch model on a slow processor takes several seconds for
// every utterance.
constexpr int answerTimeoutMs = 60000;

// 100 ms is quiet at this share of the loudest 100 ms of its piece, as RMS:
// 20 dB under it.
constexpr float kQuietShareOfLoudest = 0.1f;

float rmsOf(QByteArrayView pcm)
{
    const qsizetype samples = pcm.size() / 2;
    if (samples == 0) {
        return 0.0f;
    }
    double sum = 0.0;
    for (qsizetype index = 0; index < samples; ++index) {
        const double sample = qFromLittleEndian<qint16>(pcm.constData() + 2 * index) / 32768.0;
        sum += sample * sample;
    }
    return float(std::sqrt(sum / double(samples)));
}

enum class UtteranceMark { None, Begin, End };

// Audio for the live attempt, then the utterance mark that came after it.
struct PendingInput {
    QByteArray pcm;
    UtteranceMark then = UtteranceMark::None;
};

// What one attempt runs with on the worker.
struct LocalAttempt {
    quint64 id = 0;
    QString modelPath;
    LocalRunsOn runsOn;
    QString spokenLanguage;
    // Whether it asked for segment timings.
    bool timed = false;
    // A recording's: see SpeechAttemptOptions::cutIntoUtterances.
    bool cutIntoUtterances = false;
};

} // namespace

// One transcriber's attempts, on the worker thread. The live attempt and the
// pending input are shared with the transcriber's thread; everything else
// belongs to the worker. The transcriber queues calls here and hears back
// through the signals, so work queued for a transcriber that has gone still
// runs safely and reaches no one.
class LocalTranscriberWork final : public QObject {
    Q_OBJECT

public:
    explicit LocalTranscriberWork(LocalSpeechWorker &worker)
        : m_worker(worker)
        // Whatever the worker is running stops once the caller moves on.
        , m_client(worker, [this] { return abandoned(); })
    {
    }

    // Any thread. The attempt the caller still wants results for, 0 for none.
    quint64 liveAttempt() const { return m_liveAttempt.load(); }
    // Makes attemptId the live attempt, dropping input pending for the one
    // before, and returns that one.
    quint64 setLiveAttempt(quint64 attemptId)
    {
        const QMutexLocker lock(&m_pendingMutex);
        m_pending.clear();
        return m_liveAttempt.exchange(attemptId);
    }
    // Set on destruction so a running Speed Test stops too, at the next point
    // transcribe.cpp checks: between chunks and decode steps, not mid-encoder.
    void shutDown() { m_shuttingDown = true; }
    // When inference falls behind the microphone, the worker takes everything
    // that piled up at once instead of working through a backlog of small
    // chunks.
    void queueInput(quint64 attemptId, const QByteArray &pcm, UtteranceMark then = UtteranceMark::None);

    // Worker thread.
    void begin(const LocalAttempt &attempt);
    void takePending();
    void finish(quint64 attemptId);
    void release() { m_client.release(); }
    void runSpeedTest(const QString &modelId, const QString &modelPath, const LocalRunsOn &runsOn);

signals:
    void connected(quint64 attemptId);
    void partial(quint64 attemptId, const QString &text);
    void finalText(quint64 attemptId, const QString &text);
    void transcript(quint64 attemptId, const QString &text);
    void segments(quint64 attemptId, const QList<speecher::TranscriptSegment> &segments);
    void completed(quint64 attemptId);
    void failed(const speecher::SpeechFailure &failure);
    void speedTestFinished(const QString &modelId, double seconds, const QString &error);

private:
    bool abandoned() const;
    bool running() const;
    void take(const QByteArray &pcm);
    bool feed(QByteArrayView pcm);
    bool endPiece();
    std::optional<QString> transcribeInPieces(const QByteArray &pcm, QList<TranscriptSegment> *segments,
                                              QString *error);
    void beginUtterance();
    void endUtterance();
    void fail(const QString &message, SpeechPhase phase, ProviderFailureKind kind);
    void complete();

    LocalSpeechWorker &m_worker;
    std::atomic<quint64> m_liveAttempt = 0;
    std::atomic_bool m_shuttingDown = false;
    QMutex m_pendingMutex;
    QList<PendingInput> m_pending;
    bool m_takeQueued = false;

    // Everything below belongs to the worker thread.
    LocalSpeechWorker::Client m_client;
    LocalAttempt m_attempt;
    bool m_attemptFailed = false;
    bool m_speedTesting = false;
    // A dictation's: how much of the stream's committed text went out.
    qsizetype m_emittedCommittedChars = 0;
    // A stream's: where its current piece ends, and the text of the pieces
    // before it, which a dictation has sent as finals already.
    AudioPieces m_pieces;
    QString m_earlierPieces;
    // A batch model's audio: the dictation's, or the open utterance's.
    QByteArray m_batchPcm;
    // A recording's: between beginUtterance() and endUtterance().
    bool m_utteranceOpen = false;
    // A recording's: the quiet just before the next utterance, which goes
    // in with it so a first sound softer than what counts as voice is not
    // cut. The rest of the quiet goes in nowhere.
    QByteArray m_leadIn;
};

AudioPieces::Fit AudioPieces::fit(QByteArrayView pcm)
{
    for (qsizetype at = 0; at < pcm.size();) {
        const QByteArrayView window =
            pcm.sliced(at, std::min({kSpeechChunkBytes, pcm.size() - at, kLongestPieceBytes - m_bytes}));
        at += window.size();
        m_bytes += window.size();
        const float rms = rmsOf(window);
        m_loudest = std::max(m_loudest, rms);
        const bool quiet = rms <= m_loudest * kQuietShareOfLoudest;
        if (m_bytes >= kLongestPieceBytes || (m_bytes >= kShortestPieceBytes && quiet)) {
            *this = {};
            return {at, true};
        }
    }
    return {pcm.size(), false};
}

QString joinPieceTexts(const QString &before, const QString &after)
{
    const QString left = before.trimmed();
    const QString right = after.trimmed();
    if (left.isEmpty() || right.isEmpty()) {
        return left + right;
    }
    const bool spaced = !writtenWithoutSpaces(left.back().script()) && !writtenWithoutSpaces(right.front().script());
    return spaced ? left + QLatin1Char(' ') + right : left + right;
}

StreamSplit splitStreamText(const LocalSpeechEngine::StreamText &text, qsizetype finalChars)
{
    StreamSplit split{{}, {}, finalChars};
    const qsizetype wordEnd = text.committed.lastIndexOf(QLatin1Char(' '));
    if (wordEnd > finalChars) {
        split.finalWords = text.committed.mid(finalChars, wordEnd - finalChars);
        split.finalChars = wordEnd;
    }
    split.partial = text.committed.mid(split.finalChars) + text.tentative;
    return split;
}

void LocalTranscriberWork::queueInput(quint64 attemptId, const QByteArray &pcm, UtteranceMark then)
{
    const QMutexLocker lock(&m_pendingMutex);
    if (attemptId != m_liveAttempt.load()) {
        return;
    }
    if (m_pending.isEmpty() || m_pending.last().then != UtteranceMark::None) {
        m_pending.append(PendingInput{});
    }
    m_pending.last().pcm.append(pcm);
    m_pending.last().then = then;
    if (!std::exchange(m_takeQueued, true)) {
        QMetaObject::invokeMethod(this, [this] { takePending(); }, Qt::QueuedConnection);
    }
}

// The Speed Test stops for an attempt of any transcriber.
bool LocalTranscriberWork::abandoned() const
{
    if (m_shuttingDown) {
        return true;
    }
    return m_speedTesting ? m_worker.attemptRunning() : m_liveAttempt.load() != m_attempt.id;
}

bool LocalTranscriberWork::running() const
{
    return m_attempt.id != 0 && !m_attemptFailed && m_attempt.id == m_liveAttempt.load();
}

void LocalTranscriberWork::fail(const QString &message, SpeechPhase phase, ProviderFailureKind kind)
{
    m_attemptFailed = true;
    m_client.release();
    emit failed({m_attempt.id, message, false, phase, kind});
}

void LocalTranscriberWork::complete()
{
    m_client.release();
    emit completed(m_attempt.id);
}

void LocalTranscriberWork::begin(const LocalAttempt &attempt)
{
    if (attempt.id != m_liveAttempt.load()) {
        return;
    }
    m_attempt = attempt;
    m_attemptFailed = false;
    m_emittedCommittedChars = 0;
    m_pieces = {};
    m_earlierPieces.clear();
    m_batchPcm.clear();
    m_utteranceOpen = false;
    m_leadIn.clear();
    QString error;
    // A recording's stream begins with each utterance.
    if (!m_client.hold(attempt.modelPath, attempt.runsOn, &error)
        || (!attempt.cutIntoUtterances && m_client.streams()
            && !m_client.session().beginStream(attempt.spokenLanguage, &error))) {
        fail(error, SpeechPhase::Load, ProviderFailureKind::Unavailable);
        return;
    }
    // A recording starts once its streams have connected, so a loaded model
    // says so. Dictation never waited for it.
    if (attempt.cutIntoUtterances) {
        emit connected(attempt.id);
    }
    takePending();
}

void LocalTranscriberWork::takePending()
{
    QList<PendingInput> inputs;
    {
        const QMutexLocker lock(&m_pendingMutex);
        m_takeQueued = false;
        // Input for an attempt the worker has not begun yet stays pending;
        // begin() takes it.
        if (m_liveAttempt.load() != m_attempt.id) {
            return;
        }
        inputs.swap(m_pending);
    }
    for (const PendingInput &input : std::as_const(inputs)) {
        if (!running()) {
            return;
        }
        take(input.pcm);
        if (input.then == UtteranceMark::Begin) {
            beginUtterance();
        } else if (input.then == UtteranceMark::End) {
            endUtterance();
        }
    }
}

void LocalTranscriberWork::take(const QByteArray &pcm)
{
    if (pcm.isEmpty() || !running()) {
        return;
    }
    if (m_attempt.cutIntoUtterances && !m_utteranceOpen) {
        m_leadIn = (m_leadIn + pcm).right(kUtteranceLeadInBytes);
        return;
    }
    // A batch model's pieces are cut once all of the audio is in.
    if (!m_client.streams()) {
        m_batchPcm.append(pcm);
        return;
    }
    for (QByteArrayView rest(pcm); !rest.isEmpty() && running();) {
        const AudioPieces::Fit fit = m_pieces.fit(rest);
        if (!feed(rest.first(fit.bytes)) || (fit.ends && !endPiece())) {
            return;
        }
        rest = rest.sliced(fit.bytes);
    }
}

// Streams audio of the current piece. False when the stream failed or was
// aborted.
bool LocalTranscriberWork::feed(QByteArrayView pcm)
{
    LocalSpeechEngine::StreamText text;
    QString error;
    if (!m_client.session().feed(pcm.toByteArray(), &text, &error)) {
        if (!error.isEmpty()) {
            fail(error, SpeechPhase::Streaming, ProviderFailureKind::Other);
        }
        return false;
    }
    if (m_attempt.cutIntoUtterances) {
        emit partial(m_attempt.id, joinPieceTexts(m_earlierPieces, text.committed + text.tentative));
        return true;
    }
    const StreamSplit split = splitStreamText(text, m_emittedCommittedChars);
    m_emittedCommittedChars = split.finalChars;
    if (!split.finalWords.isEmpty()) {
        emit finalText(m_attempt.id, split.finalWords);
    }
    emit partial(m_attempt.id, split.partial);
    return true;
}

// Finalizes the stream's piece, whose text joins the pieces before, and
// begins a stream for the next. A dictation sends the piece's words that had
// not gone out as finals. False when the stream failed or was aborted.
bool LocalTranscriberWork::endPiece()
{
    LocalSpeechEngine::Session &session = m_client.session();
    QString error;
    const std::optional<QString> text = session.finalize(&error);
    if (!text || !session.beginStream(m_attempt.spokenLanguage, &error)) {
        if (!error.isEmpty()) {
            fail(error, SpeechPhase::Streaming, ProviderFailureKind::Other);
        }
        return false;
    }
    if (!m_attempt.cutIntoUtterances) {
        if (const QString rest = text->mid(m_emittedCommittedChars).trimmed(); !rest.isEmpty()) {
            emit finalText(m_attempt.id, rest);
        }
        emit partial(m_attempt.id, {});
        m_emittedCommittedChars = 0;
    }
    m_earlierPieces = joinPieceTexts(m_earlierPieces, *text);
    return true;
}

// A batch model's text for pcm, a piece at a time, with each piece's
// segments, when asked for, moved to where the piece starts.
std::optional<QString> LocalTranscriberWork::transcribeInPieces(const QByteArray &pcm,
                                                                QList<TranscriptSegment> *segments,
                                                                QString *error)
{
    LocalSpeechEngine::Session &session = m_client.session();
    AudioPieces pieces;
    QString text;
    for (qsizetype start = 0; start < pcm.size();) {
        const QByteArray piece = pcm.mid(start, pieces.fit(QByteArrayView(pcm).sliced(start)).bytes);
        QList<TranscriptSegment> timed;
        const std::optional<QString> pieceText = segments
            ? session.transcribeTimed(piece, m_attempt.spokenLanguage, &timed, error)
            : session.transcribe(piece, m_attempt.spokenLanguage, error);
        if (!pieceText) {
            return std::nullopt;
        }
        const qint64 startMs = start * 1000 / kSpeechBytesPerSecond;
        for (TranscriptSegment &segment : timed) {
            segment.startMs += startMs;
            segment.endMs += startMs;
            segments->append(segment);
        }
        text = joinPieceTexts(text, *pieceText);
        start += piece.size();
    }
    return text;
}

void LocalTranscriberWork::beginUtterance()
{
    if (!m_attempt.cutIntoUtterances || m_utteranceOpen) {
        return;
    }
    m_utteranceOpen = true;
    m_pieces = {};
    m_earlierPieces.clear();
    QString error;
    if (m_client.streams() && !m_client.session().beginStream(m_attempt.spokenLanguage, &error)) {
        fail(error, SpeechPhase::Streaming, ProviderFailureKind::Other);
        return;
    }
    take(std::exchange(m_leadIn, {}));
}

// Every utterance is a final, one with no words too, so the caller knows it
// was answered.
void LocalTranscriberWork::endUtterance()
{
    if (!m_attempt.cutIntoUtterances || !m_utteranceOpen) {
        return;
    }
    m_utteranceOpen = false;
    QString error;
    const std::optional<QString> text = m_client.streams()
        ? m_client.session().finalize(&error)
        : transcribeInPieces(std::exchange(m_batchPcm, {}), nullptr, &error);
    if (!text) {
        if (!error.isEmpty()) {
            fail(error, SpeechPhase::Finalize, ProviderFailureKind::Other);
        }
        return;
    }
    emit finalText(m_attempt.id, joinPieceTexts(std::exchange(m_earlierPieces, {}), *text));
}

void LocalTranscriberWork::finish(quint64 attemptId)
{
    if (attemptId != m_attempt.id || !running()) {
        return;
    }
    // The utterance still open is the recording's last line, and the finals
    // are the whole transcript.
    if (m_attempt.cutIntoUtterances) {
        endUtterance();
        if (running()) {
            complete();
        }
        return;
    }
    QString error;
    // The final text can revise what streamed, so it replaces all of it.
    // A stream's final text comes from its committed text, which its raw
    // rows may not match, so only a batch model's run is timed.
    QList<TranscriptSegment> timed;
    std::optional<QString> text;
    if (m_client.streams()) {
        text = m_client.session().finalize(&error);
        if (text) {
            text = joinPieceTexts(m_earlierPieces, *text);
        }
    } else {
        text = transcribeInPieces(std::exchange(m_batchPcm, {}), m_attempt.timed ? &timed : nullptr, &error);
    }
    if (!text) {
        if (!error.isEmpty()) {
            fail(error, SpeechPhase::Finalize, ProviderFailureKind::Other);
        }
        return;
    }
    if (!timed.isEmpty()) {
        emit segments(attemptId, timed);
    }
    emit transcript(attemptId, *text);
    complete();
}

void LocalTranscriberWork::runSpeedTest(const QString &modelId, const QString &modelPath, const LocalRunsOn &runsOn)
{
    m_speedTesting = true;
    QString error;
    std::optional<double> seconds;
    if (m_client.hold(modelPath, runsOn, &error)) {
        seconds = speedTestSeconds(m_client.session(), &error);
        if (!seconds && error.isEmpty()) {
            error = QStringLiteral("A dictation or recording started, so the speed test stopped.");
        }
        m_client.release();
    }
    m_speedTesting = false;
    emit speedTestFinished(modelId, seconds.value_or(0), error);
}

LocalSpeechTranscriber::LocalSpeechTranscriber(const LocalModelStore &store,
                                               std::shared_ptr<LocalSpeechWorker> worker,
                                               QObject *parent)
    : SpeechTranscriber(parent)
    , m_store(store)
    , m_worker(std::move(worker))
    , m_work(new LocalTranscriberWork(*m_worker))
{
    m_worker->adopt(m_work);
    // Text for an attempt the caller has moved on from goes nowhere.
    connect(m_work, &LocalTranscriberWork::connected, this, [this](quint64 attemptId) {
        if (isLive(attemptId)) emit attemptConnected(attemptId);
    });
    connect(m_work, &LocalTranscriberWork::partial, this, [this](quint64 attemptId, const QString &text) {
        if (isLive(attemptId)) emit partialTranscript(attemptId, text);
    });
    connect(m_work, &LocalTranscriberWork::finalText, this, [this](quint64 attemptId, const QString &text) {
        if (isLive(attemptId)) emit finalTranscript(attemptId, text);
    });
    connect(m_work, &LocalTranscriberWork::transcript, this, [this](quint64 attemptId, const QString &text) {
        if (isLive(attemptId)) emit attemptTranscript(attemptId, text);
    });
    connect(m_work, &LocalTranscriberWork::segments, this,
            [this](quint64 attemptId, const QList<TranscriptSegment> &timed) {
                if (isLive(attemptId)) emit attemptSegments(attemptId, timed);
            });
    // The attempt's last signals: nothing for it is emitted after either.
    connect(m_work, &LocalTranscriberWork::completed, this, [this](quint64 attemptId) {
        if (isLive(attemptId)) {
            setLiveAttempt(0);
            emit attemptCompleted(attemptId);
        }
    });
    connect(m_work, &LocalTranscriberWork::failed, this, [this](const SpeechFailure &failure) {
        if (isLive(failure.attemptId)) {
            setLiveAttempt(0);
            emit failed(failure);
        }
    });
    connect(m_work, &LocalTranscriberWork::speedTestFinished, this,
            [this](const QString &modelId, double seconds, const QString &error) {
                m_worker->resumeIdleUnload();
                emit speedTestFinished(modelId, seconds, error);
            });
    connect(m_worker.get(), &LocalSpeechWorker::runsOnChanged, this, &LocalSpeechTranscriber::runsOnChanged);
}

LocalSpeechTranscriber::~LocalSpeechTranscriber()
{
    m_work->shutDown();
    setLiveAttempt(0);
    m_work->deleteLater();
}

bool LocalSpeechTranscriber::requiresRefresh(const SpeechSettings &, const AccountSettings &) const
{
    return false;
}

std::optional<ProviderFailure> LocalSpeechTranscriber::prepare(const SpeechSettings &settings, const AccountSettings &)
{
    QString error;
    if (downloadedModelPath(localModelId(settings), &error).isEmpty()) {
        return ProviderFailure{ProviderFailureKind::Unavailable, error};
    }
    return std::nullopt;
}

int LocalSpeechTranscriber::utteranceAnswerTimeoutMs() const
{
    return answerTimeoutMs;
}

// An utterance at a time with SpeechAttemptOptions::cutIntoUtterances.
bool LocalSpeechTranscriber::streamsFinalText(const SpeechSettings &) const
{
    return true;
}

void LocalSpeechTranscriber::startAttempt(quint64 attemptId, const SpeechSettings &settings,
                                          const SpeechAttemptOptions &options)
{
    setLiveAttempt(attemptId, settings.local.idleUnloadMinutes);
    QString error;
    const QString modelPath = downloadedModelPath(localModelId(settings), &error);
    if (modelPath.isEmpty()) {
        QMetaObject::invokeMethod(this, [this, attemptId, error] {
            if (isLive(attemptId)) {
                setLiveAttempt(0);
                emit failed({attemptId, error, false, SpeechPhase::Load, ProviderFailureKind::Unavailable});
            }
        }, Qt::QueuedConnection);
        return;
    }
    const LocalAttempt attempt{attemptId,
                               modelPath,
                               settings.local.runsOn,
                               settings.language,
                               options.timedSegments,
                               options.cutIntoUtterances};
    QMetaObject::invokeMethod(m_work, [work = m_work, attempt] { work->begin(attempt); }, Qt::QueuedConnection);
}

void LocalSpeechTranscriber::sendAudio(quint64 attemptId, const QByteArray &pcm)
{
    m_work->queueInput(attemptId, pcm);
}

void LocalSpeechTranscriber::beginUtterance(quint64 attemptId)
{
    m_work->queueInput(attemptId, {}, UtteranceMark::Begin);
}

void LocalSpeechTranscriber::endUtterance(quint64 attemptId)
{
    m_work->queueInput(attemptId, {}, UtteranceMark::End);
}

void LocalSpeechTranscriber::finishInput(quint64 attemptId)
{
    if (isLive(attemptId)) {
        QMetaObject::invokeMethod(m_work, [work = m_work, attemptId] { work->finish(attemptId); },
                                  Qt::QueuedConnection);
    }
}

void LocalSpeechTranscriber::cancelAttempt(quint64 attemptId)
{
    if (isLive(attemptId)) {
        setLiveAttempt(0);
        QMetaObject::invokeMethod(m_work, [work = m_work] { work->release(); }, Qt::QueuedConnection);
    }
}

void LocalSpeechTranscriber::runSpeedTest(const QString &modelId, const LocalRunsOn &runsOn)
{
    QString error;
    const QString modelPath = downloadedModelPath(modelId, &error);
    if (m_worker->attemptRunning()) {
        error = QStringLiteral("Finish dictating or recording, then test the speed again.");
    }
    if (!error.isEmpty()) {
        emit speedTestFinished(modelId, 0, error);
        return;
    }
    m_worker->pauseIdleUnload();
    QMetaObject::invokeMethod(m_work, [work = m_work, modelId, modelPath, runsOn] {
        work->runSpeedTest(modelId, modelPath, runsOn);
    }, Qt::QueuedConnection);
}

QString LocalSpeechTranscriber::downloadedModelPath(const QString &modelId, QString *error) const
{
    const LocalModel *model = findLocalModel(modelId);
    if (!model) {
        *error = QStringLiteral("Choose a local model in Services › This computer.");
        return {};
    }
    if (!m_store.isDownloaded(*model)) {
        *error = QStringLiteral("%1 is not downloaded yet. Download it in Services › This computer.").arg(model->name);
        return {};
    }
    return m_store.modelPath(*model);
}

bool LocalSpeechTranscriber::isLive(quint64 attemptId) const
{
    return attemptId != 0 && attemptId == m_work->liveAttempt();
}

void LocalSpeechTranscriber::setLiveAttempt(quint64 attemptId, int idleUnloadMinutes)
{
    const quint64 before = m_work->setLiveAttempt(attemptId);
    if (before == 0 && attemptId != 0) {
        m_worker->attemptStarted(idleUnloadMinutes);
    } else if (before != 0 && attemptId == 0) {
        m_worker->attemptEnded();
    }
}

} // namespace speecher

#include "LocalSpeechTranscriber.moc"
