#include "providers/LocalSpeechTranscriber.h"

#include "core/LocalModelCatalog.h"
#include "providers/LocalModelStore.h"

#include <algorithm>

namespace speecher {
namespace {

constexpr int msPerMinute = 60 * 1000;
// Keeps the timer's millisecond interval well inside an int.
constexpr int maxIdleUnloadMinutes = 24 * 60;

int idleUnloadMs(int minutes)
{
    return std::clamp(minutes, 0, maxIdleUnloadMinutes) * msPerMinute;
}

} // namespace

LocalSpeechTranscriber::LocalSpeechTranscriber(const LocalModelStore &store, QObject *parent)
    : SpeechTranscriber(parent)
    , m_store(store)
    // Whatever the worker is running stops once the caller moves on from it.
    , m_engine([this] { return m_shuttingDown || m_liveAttempt.load() != m_workerAttempt; })
{
    m_idleTimer.setSingleShot(true);
    // Until an attempt brings the person's setting.
    m_idleTimer.setInterval(idleUnloadMs(LocalSpeechSettings().idleUnloadMinutes));
    connect(&m_idleTimer, &QTimer::timeout, this, [this] {
        onWorker([this] {
            if (m_liveAttempt.load() == 0) {
                m_engine.unload();
            }
        });
    });
    m_thread.setObjectName(QStringLiteral("LocalSpeech"));
    m_workerContext.moveToThread(&m_thread);
    m_thread.start();
}

LocalSpeechTranscriber::~LocalSpeechTranscriber()
{
    m_shuttingDown = true;
    m_liveAttempt = 0;
    m_thread.quit();
    m_thread.wait();
}

QString LocalSpeechTranscriber::id() const
{
    return QStringLiteral("local");
}

QString LocalSpeechTranscriber::label() const
{
    return QStringLiteral("Local model");
}

bool LocalSpeechTranscriber::requiresRefresh(const SpeechSettings &) const
{
    return false;
}

SpeechPrepareResult LocalSpeechTranscriber::prepare(const SpeechSettings &settings)
{
    QString error;
    const bool ready = !downloadedModelPath(settings.local.modelId, &error).isEmpty();
    return {ready, error};
}

void LocalSpeechTranscriber::startAttempt(quint64 attemptId, const SpeechSettings &settings)
{
    m_idleTimer.stop();
    m_idleTimer.setInterval(idleUnloadMs(settings.local.idleUnloadMinutes));
    {
        const QMutexLocker lock(&m_pendingMutex);
        m_liveAttempt = attemptId;
        m_pendingPcm.clear();
    }
    QString error;
    const QString modelPath = downloadedModelPath(settings.local.modelId, &error);
    if (modelPath.isEmpty()) {
        reportEnd(attemptId, [this, attemptId, error] {
            emit failed({attemptId, error, false, QStringLiteral("load")});
        });
        return;
    }
    onWorker([this, attemptId, modelPath, deviceId = settings.local.deviceId] {
        begin(attemptId, modelPath, deviceId);
    });
}

void LocalSpeechTranscriber::sendAudio(quint64 attemptId, const QByteArray &pcm)
{
    const QMutexLocker lock(&m_pendingMutex);
    if (attemptId != m_liveAttempt.load()) {
        return;
    }
    m_pendingPcm.append(pcm);
    if (!m_feedQueued) {
        m_feedQueued = true;
        onWorker([this] { feedPending(); });
    }
}

void LocalSpeechTranscriber::finishInput(quint64 attemptId)
{
    if (attemptId == m_liveAttempt.load()) {
        onWorker([this, attemptId] { finish(attemptId); });
    }
}

void LocalSpeechTranscriber::cancelAttempt(quint64 attemptId)
{
    if (attemptId == m_liveAttempt.load()) {
        m_liveAttempt = 0;
        startIdleTimer();
    }
}

void LocalSpeechTranscriber::runSpeedTest(const QString &modelId, const QString &deviceId)
{
    QString error;
    const QString modelPath = downloadedModelPath(modelId, &error);
    if (m_liveAttempt.load() != 0) {
        error = QStringLiteral("Finish dictating, then test the speed again.");
    }
    if (!error.isEmpty()) {
        emit speedTestFinished(modelId, 0, error);
        return;
    }
    m_idleTimer.stop();
    onWorker([this, modelId, modelPath, deviceId] {
        m_workerAttempt = 0;
        QString error;
        std::optional<double> seconds;
        if (ensureLoaded(modelPath, deviceId, &error)) {
            seconds = m_engine.speedTestSeconds(&error);
            if (!seconds && error.isEmpty()) {
                error = QStringLiteral("Dictation started, so the speed test stopped.");
            }
        }
        onCaller([this, modelId, seconds, error] {
            if (m_liveAttempt.load() == 0) {
                startIdleTimer();
            }
            emit speedTestFinished(modelId, seconds.value_or(0), error);
        });
    });
}

QString LocalSpeechTranscriber::downloadedModelPath(const QString &modelId, QString *error) const
{
    const LocalModel *model = findLocalModel(modelId);
    if (!model) {
        *error = QStringLiteral("Choose a local model on the Local models page.");
        return {};
    }
    if (!m_store.isDownloaded(*model)) {
        *error = QStringLiteral("%1 is not downloaded yet. Download it on the Local models page.")
                     .arg(model->name);
        return {};
    }
    return m_store.modelPath(*model);
}

void LocalSpeechTranscriber::onWorker(std::function<void()> work)
{
    QMetaObject::invokeMethod(&m_workerContext, std::move(work), Qt::QueuedConnection);
}

void LocalSpeechTranscriber::onCaller(std::function<void()> work)
{
    QMetaObject::invokeMethod(this, std::move(work), Qt::QueuedConnection);
}

void LocalSpeechTranscriber::report(quint64 attemptId, std::function<void()> emitSignal)
{
    onCaller([this, attemptId, emitSignal = std::move(emitSignal)] {
        if (attemptId == m_liveAttempt.load()) {
            emitSignal();
        }
    });
}

// The attempt's last signal: nothing for it is emitted after this one.
void LocalSpeechTranscriber::reportEnd(quint64 attemptId, std::function<void()> emitSignal)
{
    onCaller([this, attemptId, emitSignal = std::move(emitSignal)] {
        if (attemptId != m_liveAttempt.load()) {
            return;
        }
        m_liveAttempt = 0;
        startIdleTimer();
        emitSignal();
    });
}

void LocalSpeechTranscriber::startIdleTimer()
{
    if (m_idleTimer.interval() > 0) {
        m_idleTimer.start();
    }
}

bool LocalSpeechTranscriber::ensureLoaded(const QString &modelPath, const QString &deviceId, QString *error)
{
    return m_engine.isLoaded(modelPath, deviceId) || m_engine.load(modelPath, deviceId, error);
}

bool LocalSpeechTranscriber::attemptRunning(quint64 attemptId) const
{
    return attemptId == m_workerAttempt && !m_workerAttemptFailed
        && attemptId == m_liveAttempt.load();
}

void LocalSpeechTranscriber::failAttempt(quint64 attemptId, const QString &message, const QString &phase)
{
    m_workerAttemptFailed = true;
    reportEnd(attemptId, [this, attemptId, message, phase] {
        emit failed({attemptId, message, false, phase});
    });
}

void LocalSpeechTranscriber::begin(quint64 attemptId, const QString &modelPath, const QString &deviceId)
{
    if (attemptId != m_liveAttempt.load()) {
        return;
    }
    m_workerAttempt = attemptId;
    m_workerAttemptFailed = false;
    m_batchPcm.clear();
    m_emittedCommittedChars = 0;
    QString error;
    if (!ensureLoaded(modelPath, deviceId, &error)
        || (m_engine.streams() && !m_engine.beginStream(&error))) {
        failAttempt(attemptId, error, QStringLiteral("load"));
        return;
    }
    feedPending();
}

void LocalSpeechTranscriber::feedPending()
{
    quint64 attemptId = 0;
    QByteArray pcm;
    {
        const QMutexLocker lock(&m_pendingMutex);
        m_feedQueued = false;
        attemptId = m_liveAttempt.load();
        // Audio for an attempt the worker has not begun yet stays pending;
        // begin() takes it.
        if (attemptId != m_workerAttempt) {
            return;
        }
        pcm.swap(m_pendingPcm);
    }
    if (pcm.isEmpty() || !attemptRunning(attemptId)) {
        return;
    }
    if (!m_engine.streams()) {
        m_batchPcm.append(pcm);
        return;
    }
    LocalSpeechEngine::StreamText text;
    QString error;
    if (!m_engine.feed(pcm, &text, &error)) {
        if (!error.isEmpty()) {
            failAttempt(attemptId, error, QStringLiteral("streaming"));
        }
        return;
    }
    // Committed text can end mid-word, and each final is joined to the next
    // with a space, so only whole words become final.
    const qsizetype wordEnd = text.committed.lastIndexOf(QLatin1Char(' '));
    if (wordEnd > m_emittedCommittedChars) {
        const QString words = text.committed.mid(m_emittedCommittedChars, wordEnd - m_emittedCommittedChars);
        m_emittedCommittedChars = wordEnd;
        report(attemptId, [this, attemptId, words] { emit finalTranscript(attemptId, words); });
    }
    const QString partial = text.committed.mid(m_emittedCommittedChars) + text.tentative;
    report(attemptId, [this, attemptId, partial] { emit partialTranscript(attemptId, partial); });
}

void LocalSpeechTranscriber::finish(quint64 attemptId)
{
    if (!attemptRunning(attemptId)) {
        return;
    }
    QString error;
    // The final text can revise what streamed, so it replaces all of it.
    const std::optional<QString> transcript = m_engine.streams()
        ? m_engine.finalize(&error)
        : m_engine.transcribe(m_batchPcm, &error);
    m_batchPcm.clear();
    if (!transcript) {
        if (!error.isEmpty()) {
            failAttempt(attemptId, error, QStringLiteral("finalize"));
        }
        return;
    }
    reportEnd(attemptId, [this, attemptId, text = *transcript] {
        emit attemptTranscript(attemptId, text);
        emit attemptCompleted(attemptId);
    });
}

} // namespace speecher
