#pragma once

#include "transcribe/FileTranscriptionSession.h"
#include "transcribe/TranscribePresentation.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVector>

namespace speecher {

class ApplicationController;

// The Transcribe page's work: the files listed for the next batch and its
// choices, the batch while it runs, and its results. A batch starts through
// the controller, so it and dictation never run at once, and its files,
// choices and labels are copied when it starts, so a later change describes
// the next batch, not this one.
class TranscribeQueue : public QObject {
    Q_OBJECT

public:
    // The setup form, the running batch and its results.
    enum class Step { Configure, Transcribe, Export };
    enum class FileState { Waiting, Current, Done, Failed };

    explicit TranscribeQueue(ApplicationController *controller, QObject *parent = nullptr);

    Step step() const;

    // Setup: the files the next batch transcribes, by absolute path.
    const QStringList &files() const;
    // Lists each file the decoder may take, once. Files added over finished
    // results start the next batch's list.
    void addFiles(const QStringList &paths);
    void removeFile(const QString &path);
    // A file's length once known, otherwise -1.
    qint64 durationMs(const QString &path) const;

    // The next batch's choices, seeded from the settings dictation uses and
    // never written back to them. Until one is chosen here they follow the
    // settings; a profile, cleanup level or tone chosen here that the
    // settings stop offering gives way to the profile's own.
    const TranscribeOptions &options() const;
    void setOptions(const TranscribeOptions &options);
    // Picks a Writing Profile and takes its services, cleanup level and tone,
    // as dictation would.
    void chooseProfile(const QString &profile);
    // The model the chosen refinement provider runs for the chosen profile,
    // or empty when the provider names none.
    QString refinementModel() const;

    // Transcribes the listed files. False, with problem() saying why, when it
    // cannot start.
    bool start();
    // Stops the batch or the retry; files already finished keep their results.
    void cancel();
    // Back to setup: the finished files leave the list and the choices start
    // over from the settings.
    void backToSetup();

    // The batch as it started.
    const QStringList &batch() const;
    const TranscribeOptions &batchOptions() const;
    const TranscribeBatchLabels &batchLabels() const;
    // Whether the batch refines, so its results offer the raw text too.
    bool batchRefines() const;
    // The file being transcribed, or -1 before the first.
    int current() const;
    FileState fileState(int index) const;
    TranscribePhase phase() const;
    // The current file's peak levels, 0..1 across its length, once decoded.
    const QVector<float> &peaks() const;
    const QString &partialText() const;
    // How far the current file is through all of its work, 0..1. It never
    // moves back within a file, eases on with time through the waits, and
    // reaches 1 only once the file has finished, so call it again to follow.
    qreal progress();

    // Each file that finished or failed, in batch order.
    const QList<TranscribeFileResult> &results() const;
    // The batch was cancelled before every file was done.
    bool cancelled() const;
    // Runs one failed file again with the batch's choices; its result takes
    // the failed one's place.
    void retry(int index);
    // The result a retry is running for, or -1.
    int retrying() const;
    // Why the last start, retry or export did not happen; empty once one does.
    const QString &problem() const;
    void setProblem(const QString &problem);

signals:
    void filesChanged();
    void optionsChanged();
    void stepChanged();
    // The current file started, entered a phase, was decoded or heard more.
    void fileChanged();
    void resultsChanged();
    void problemChanged();

private:
    void seedOptions();
    void dropWithdrawnChoices();
    void setStep(Step step);
    void resetFile();
    void enter(TranscribePhase phase);
    // Whether a session event belongs to the batch shown: a retry's run
    // reports on its result instead.
    bool followsBatch() const;

    ApplicationController *m_controller;
    Step m_step = Step::Configure;
    QStringList m_files;
    QHash<QString, qint64> m_durationsMs;
    TranscribeOptions m_options;
    // A choice was made here since the options were last seeded.
    bool m_optionsChosen = false;

    QStringList m_batch;
    TranscribeOptions m_batchOptions;
    TranscribeBatchLabels m_batchLabels;
    bool m_batchRefines = false;
    int m_current = -1;
    TranscribePhase m_phase = TranscribePhase::Reading;
    QElapsedTimer m_phaseClock;
    qreal m_fractionSent = 0.0;
    ForwardProgress m_progress;
    bool m_fileDone = false;
    QVector<float> m_peaks;
    QString m_partial;

    QList<TranscribeFileResult> m_results;
    bool m_cancelled = false;
    int m_retrying = -1;
    QString m_problem;
};

} // namespace speecher
