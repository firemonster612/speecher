#pragma once

#include "transcribe/FileTranscriptionSession.h"
#include "transcribe/TranscribePresentation.h"

#include <QTimer>
#include <QWidget>

class QAbstractButton;
class QButtonGroup;
class QComboBox;
class QCheckBox;
class QFrame;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QWidget;

namespace speecher {

class ApplicationController;
class InlineMessage;
class TranscribeLoomWidget;
class TranscribeModel;

// Pick audio and video files, choose how to transcribe them, watch them go,
// read the results. A view of the controller's TranscribeModel: every
// Transcribe page shows the same files and batch. Its options start from the
// user's settings and never write back.
class TranscribePage : public QWidget {
    Q_OBJECT

public:
    explicit TranscribePage(ApplicationController *controller, QWidget *parent = nullptr);

    // Lists the files (media only, no duplicates), ready to start.
    void addFiles(const QStringList &paths);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void showStep();
    void refreshSteps(TranscribeStep current);
    void seedOptionsFromSettings();
    // Offers the built-in and custom cleanup levels and tones in the settings.
    void showChoices(const AppSettings &settings);
    void applyWritingProfile();
    void refreshRefinementRows();
    void refreshOutputRows();
    void refreshFileList();
    void startBatch();
    void refreshFile();
    void refreshProgress();
    void refreshQueue();
    void showResults();
    // Stacks every result's details under its name when the card is too
    // narrow for them beside it.
    void applyResultsWidth();
    void exportAll();
    void exportOne(const QString &audioPath, const QString &text);
    bool showingRaw() const;
    TranscribeOptions options() const;

    ApplicationController *m_controller;
    TranscribeModel *m_model;
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
    QWidget *m_cleanupButtons;
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

    // Processing
    QLabel *m_processingHeader;
    TranscribeLoomWidget *m_loom;
    QPlainTextEdit *m_partial;
    QLabel *m_phase;
    QLabel *m_percent;
    QFrame *m_queueCard;
    // The step on screen, to tell where a change of step came from.
    TranscribeStep m_shownStep = TranscribeStep::Configure;
    // The file the loom is drawing, so a new one restarts it.
    int m_loomFile = -1;
    // Eases the open-ended waits forward between engine signals.
    QTimer m_progressTimer;

    // Results
    QLabel *m_resultsHeader;
    QLabel *m_summary;
    InlineMessage *m_problem;
    QWidget *m_variants;
    QAbstractButton *m_showRefined = nullptr;
    QFrame *m_resultsCard;
};

} // namespace speecher
