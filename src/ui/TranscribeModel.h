#pragma once

#include "transcribe/FileTranscriptionSession.h"
#include "transcribe/TranscribePresentation.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QVector>

#include <functional>

namespace speecher {

class ApplicationController;

// What the Transcribe page shows: the listed files, the running batch and its
// results. One per controller, so the main window's Transcribe page and the
// compact Transcribe window show the same files and the same batch, as the
// macOS and Windows panes do. Each view keeps only its own setup choices.
class TranscribeModel : public QObject {
    Q_OBJECT

public:
    // The controller's model, made on first use.
    static TranscribeModel *of(ApplicationController *controller);

    TranscribeStep step() const;

    // Setup: the files the next batch will transcribe.
    const QStringList &files() const;
    // Lists the files the decoder can take, once each. Files added over
    // finished results start the next batch's list.
    void addFiles(const QStringList &paths);
    void removeFile(const QString &path);
    // A file's length once known, otherwise -1.
    qint64 durationMs(const QString &path) const;
    // Transcribes the listed files. False, with why, when it cannot start.
    bool start(const TranscribeOptions &options, QString *error);
    void cancel();
    // Back to setup; the finished files leave the list.
    void backToSetup();

    // The running batch.
    const QStringList &batch() const;
    // The file being transcribed, or -1 before the first.
    int current() const;
    TranscribePhase phase() const;
    const QVector<float> &peaks() const;
    const QString &partialText() const;
    // How far the current file is through all of its work, 0..1; never moves
    // back within a file, and reaches 1 only once the file has finished.
    qreal progress();

    // Its results.
    const QList<TranscribeFileResult> &results() const;
    // Whether the batch refines, so its results offer the raw text too.
    bool batchRefines() const;
    QString summary() const;
    QString subtitlesNote() const;
    // Runs one failed file again with the batch's choices.
    void retry(int index);
    // The result row a retry is running for, or -1.
    int retrying() const;
    // Why the last export or retry did not happen; empty once one does.
    const QString &problem() const;
    void setProblem(const QString &problem);

signals:
    void filesChanged();
    void stepChanged();
    // The current file started, entered a phase, was decoded or heard more.
    void fileChanged();
    // The current file is done; views run its playhead to the end while the
    // next file's events wait.
    void fileFinished();
    void resultsChanged();
    void problemChanged();

private:
    explicit TranscribeModel(ApplicationController *controller);

    void setStep(TranscribeStep step);
    // Clears what the view shows of the file before the next one.
    void resetFile();
    void enter(TranscribePhase phase);
    // Runs a session event now, or once the finished file before it has had
    // its landing hold, so the next file never replaces one still settling.
    void deliver(std::function<void()> event);
    void releaseHeld();

    ApplicationController *m_controller;
    TranscribeStep m_step = TranscribeStep::Configure;
    QStringList m_files;
    QHash<QString, qint64> m_durationsMs;

    QStringList m_batch;
    TranscribeOptions m_batchOptions;
    TranscribeBatchLabels m_batchLabels;
    bool m_batchRefines = false;
    int m_current = -1;
    QString m_currentPath;
    TranscribePhase m_phase = TranscribePhase::Reading;
    QElapsedTimer m_phaseClock;
    qreal m_fractionSent = 0.0;
    ForwardProgress m_progress;
    bool m_fileDone = false;
    QVector<float> m_peaks;
    QString m_partial;
    bool m_holding = false;
    QList<std::function<void()>> m_held;

    QList<TranscribeFileResult> m_results;
    bool m_cancelled = false;
    int m_retrying = -1;
    QString m_problem;
};

} // namespace speecher
