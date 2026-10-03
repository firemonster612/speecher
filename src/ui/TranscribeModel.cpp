#include "ui/TranscribeModel.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"

#include <QFileInfo>
#include <QTimer>

namespace speecher {

TranscribeModel *TranscribeModel::of(ApplicationController *controller)
{
    auto *model = controller->findChild<TranscribeModel *>(QString(), Qt::FindDirectChildrenOnly);
    return model ? model : new TranscribeModel(controller);
}

TranscribeModel::TranscribeModel(ApplicationController *controller)
    : QObject(controller)
    , m_controller(controller)
{
    FileTranscriptionSession *session = controller->fileTranscription();
    connect(session, &FileTranscriptionSession::fileStarted, this, [this](int index, const QString &path) {
        deliver([this, index, path] {
            m_current = index;
            m_currentPath = path;
            resetFile();
            enter(TranscribePhase::Reading);
        });
    });
    connect(session, &FileTranscriptionSession::fileDecoded, this,
            [this](int, const QVector<float> &peaks, qint64 durationMs) {
                deliver([this, peaks, durationMs] {
                    m_durationsMs.insert(m_currentPath, durationMs);
                    m_peaks = peaks;
                    enter(TranscribePhase::Transcribing);
                });
            });
    connect(session, &FileTranscriptionSession::fileProgress, this, [this](int, qreal fraction) {
        deliver([this, fraction] {
            m_fractionSent = fraction;
            if (fraction >= 1.0 && m_phase == TranscribePhase::Transcribing) {
                enter(TranscribePhase::Finishing);
            }
        });
    });
    connect(session, &FileTranscriptionSession::filePartialText, this, [this](int, const QString &text) {
        deliver([this, text] {
            m_partial = text;
            emit fileChanged();
        });
    });
    connect(session, &FileTranscriptionSession::fileRefining, this,
            [this] { deliver([this] { enter(TranscribePhase::Refining); }); });
    connect(session, &FileTranscriptionSession::fileFinished, this,
            [this](int, const TranscribeFileResult &result) {
                deliver([this, result] {
                    // A retry's row takes its result from batchFinished.
                    if (m_retrying >= 0) {
                        return;
                    }
                    m_fileDone = true;
                    m_results.append(result);
                    m_holding = true;
                    QTimer::singleShot(kTranscribeLandingMs, this, &TranscribeModel::releaseHeld);
                    emit fileFinished();
                });
            });
    connect(session, &FileTranscriptionSession::batchFinished, this,
            [this](const QList<TranscribeFileResult> &results, bool cancelled) {
                deliver([this, results, cancelled] {
                    m_current = -1;
                    if (m_retrying >= 0) {
                        if (!results.isEmpty() && m_retrying < m_results.size()) {
                            m_results[m_retrying] = results.first();
                        }
                        m_retrying = -1;
                        emit resultsChanged();
                        return;
                    }
                    m_cancelled = cancelled;
                    // A batch cancelled before any file finished has nothing to show.
                    setStep(m_results.isEmpty() ? TranscribeStep::Configure : TranscribeStep::Export);
                });
            });
}

TranscribeStep TranscribeModel::step() const
{
    return m_step;
}

const QStringList &TranscribeModel::files() const
{
    return m_files;
}

void TranscribeModel::addFiles(const QStringList &paths)
{
    // Files opened while a batch runs wait in the setup list it returns to.
    if (m_step == TranscribeStep::Export) {
        backToSetup();
    }
    for (const QString &path : paths) {
        const QString absolute = QFileInfo(path).absoluteFilePath();
        if (!isAudioFile(absolute) || m_files.contains(absolute)) {
            continue;
        }
        m_files << absolute;
        probeAudioDuration(absolute, this, [this, absolute](qint64 durationMs) {
            m_durationsMs.insert(absolute, durationMs);
            emit filesChanged();
        });
    }
    emit filesChanged();
}

void TranscribeModel::removeFile(const QString &path)
{
    m_files.removeOne(path);
    emit filesChanged();
}

qint64 TranscribeModel::durationMs(const QString &path) const
{
    return m_durationsMs.value(path, -1);
}

bool TranscribeModel::start(const TranscribeOptions &options, QString *error)
{
    // Refused before anything changes, so a batch or retry still running
    // keeps the list, results and choices it reports into.
    const QString refusal = m_controller->fileTranscriptionRefusal();
    if (!refusal.isEmpty()) {
        *error = refusal;
        return false;
    }
    m_batch = m_files;
    m_batchOptions = options;
    m_batchLabels = batchLabels(options, *m_controller->providerRegistry(),
                                m_controller->settings()->snapshot().refinement);
    m_results.clear();
    m_cancelled = false;
    m_current = -1;
    resetFile();
    setProblem({});
    // Before start(): a batch whose files all fail at once finishes inside it.
    setStep(TranscribeStep::Transcribe);
    if (!m_controller->startFileTranscription(m_batch, options, error)) {
        setStep(TranscribeStep::Configure);
        return false;
    }
    return true;
}

void TranscribeModel::cancel()
{
    m_controller->fileTranscription()->cancel();
}

// The finished files leave the list; files added meanwhile, and any a
// cancelled batch never reached, stay.
void TranscribeModel::backToSetup()
{
    // A retry left running would report into results no longer shown, and
    // hold up the next batch.
    if (m_retrying >= 0) {
        cancel();
    }
    for (const TranscribeFileResult &result : std::as_const(m_results)) {
        m_files.removeOne(result.path);
    }
    emit filesChanged();
    setStep(TranscribeStep::Configure);
}

const QStringList &TranscribeModel::batch() const
{
    return m_batch;
}

int TranscribeModel::current() const
{
    return m_current;
}

TranscribePhase TranscribeModel::phase() const
{
    return m_phase;
}

const QVector<float> &TranscribeModel::peaks() const
{
    return m_peaks;
}

const QString &TranscribeModel::partialText() const
{
    return m_partial;
}

qreal TranscribeModel::progress()
{
    if (m_fileDone) {
        return 1.0;
    }
    return m_progress.advance(overallFileProgress(m_fractionSent, m_phase, refinesTranscripts(m_batchOptions),
                                                  m_phaseClock.elapsed()));
}

const QList<TranscribeFileResult> &TranscribeModel::results() const
{
    return m_results;
}

const TranscribeOptions &TranscribeModel::batchOptions() const
{
    return m_batchOptions;
}

QString TranscribeModel::summary() const
{
    return batchSummary(m_results, int(m_batch.size()), m_cancelled, m_durationsMs, m_batchOptions, m_batchLabels);
}

QString TranscribeModel::subtitlesNote() const
{
    return speecher::subtitlesNote(m_results, m_batchLabels);
}

void TranscribeModel::retry(int index)
{
    m_retrying = index;
    QString error;
    if (!m_controller->startFileTranscription({m_results.at(index).path}, m_batchOptions, &error)) {
        m_retrying = -1;
        setProblem(error);
        return;
    }
    setProblem({});
    emit resultsChanged();
}

int TranscribeModel::retrying() const
{
    return m_retrying;
}

const QString &TranscribeModel::problem() const
{
    return m_problem;
}

void TranscribeModel::setProblem(const QString &problem)
{
    if (problem != m_problem) {
        m_problem = problem;
        emit problemChanged();
    }
}

void TranscribeModel::setStep(TranscribeStep step)
{
    m_step = step;
    emit stepChanged();
}

void TranscribeModel::resetFile()
{
    m_phase = TranscribePhase::Reading;
    m_phaseClock.start();
    m_fractionSent = 0.0;
    m_progress = {};
    m_fileDone = false;
    m_peaks.clear();
    m_partial.clear();
}

void TranscribeModel::enter(TranscribePhase phase)
{
    m_phase = phase;
    m_phaseClock.start();
    emit fileChanged();
}

void TranscribeModel::deliver(std::function<void()> event)
{
    if (m_holding) {
        m_held << std::move(event);
        return;
    }
    event();
}

void TranscribeModel::releaseHeld()
{
    m_holding = false;
    // Another file may finish among them; the rest wait for its hold.
    while (!m_held.isEmpty() && !m_holding) {
        m_held.takeFirst()();
    }
}

} // namespace speecher
