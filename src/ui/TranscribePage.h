#pragma once

#include "transcribe/FileTranscriptionSession.h"
#include "transcribe/TranscribePresentation.h"

#include <QElapsedTimer>
#include <QHash>
#include <QTimer>
#include <QWidget>

#include <functional>

class QAbstractButton;
class QButtonGroup;
class QComboBox;
class QCheckBox;
class QFrame;
class QLabel;
class QPushButton;
class QWidget;

namespace speecher {

class ApplicationController;
class InlineMessage;
class TranscribeLoomWidget;

// Pick audio files, choose how to transcribe them, watch them go, read the
// results. Its options start from the user's settings and never write back.
class TranscribePage : public QWidget {
    Q_OBJECT

public:
    explicit TranscribePage(ApplicationController *controller, QWidget *parent = nullptr);

    // Lists the files (audio only, no duplicates), ready to start.
    void addFiles(const QStringList &paths);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void showStage(TranscribeStep step);
    void refreshSteps(TranscribeStep current);
    void seedOptionsFromSettings();
    void applyWritingProfile();
    void refreshRefinementRows();
    void refreshOutputRows();
    void refreshFileList();
    void backToSetup();
    void startBatch();
    void retry(int index);
    void setPhase(TranscribePhase phase);
    void refreshProgress();
    // Runs a batch event now, or once the loom has finished landing the file
    // before it, so the next file never replaces a page still settling.
    void afterLanding(std::function<void()> event);
    void refreshQueue();
    void showResults();
    bool showingRaw() const;
    TranscribeOptions options() const;

    ApplicationController *m_controller;
    QList<QLabel *> m_stepLabels;
    QLabel *m_stepHint;
    QWidget *m_setup;
    QWidget *m_processing;
    QWidget *m_results;

    // Setup
    QFrame *m_filesCard;
    QComboBox *m_speech;
    QLabel *m_speechSummary;
    QCheckBox *m_vocabulary;
    QComboBox *m_refiner;
    QFrame *m_refinerModelRow;
    QLabel *m_refinerModel;
    QList<QWidget *> m_refinementDependents;
    QButtonGroup *m_cleanup;
    QComboBox *m_profile;
    QComboBox *m_tone;
    QComboBox *m_destination;
    QLabel *m_destinationSummary;
    QFrame *m_folderRow;
    QLabel *m_folderPath;
    QString m_folder;
    InlineMessage *m_startError;
    QPushButton *m_start;
    QStringList m_files;
    QHash<QString, qint64> m_durationsMs;

    // Processing
    QLabel *m_processingHeader;
    TranscribeLoomWidget *m_loom;
    QLabel *m_phase;
    QLabel *m_percent;
    QFrame *m_queueCard;
    TranscribePhase m_phaseNow = TranscribePhase::Reading;
    QElapsedTimer m_phaseClock;
    qreal m_fractionSent = 0.0;
    // Eases the open-ended waits forward between engine signals.
    QTimer m_progressTimer;
    QList<std::function<void()>> m_afterLanding;

    // Results
    QLabel *m_resultsHeader;
    QLabel *m_summary;
    QWidget *m_variants;
    QAbstractButton *m_showRefined = nullptr;
    QFrame *m_resultsCard;

    // The running or last batch.
    // True while a batch this page started runs; another Transcribe surface
    // shares the engine, and its batches are not this page's to show.
    bool m_running = false;
    QStringList m_batch;
    TranscribeOptions m_batchOptions;
    TranscribeBatchLabels m_batchLabels;
    int m_current = -1;
    QString m_currentPath;
    QList<TranscribeFileResult> m_batchResults;
    bool m_cancelled = false;
    // The result row a retry is running for, or -1.
    int m_retrying = -1;
};

} // namespace speecher
