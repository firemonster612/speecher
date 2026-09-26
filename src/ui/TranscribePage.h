#pragma once

#include "transcribe/FileTranscriptionSession.h"

#include <QHash>
#include <QWidget>

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
    enum class Stage { Setup, Processing, Results };

    void showStage(Stage stage);
    void seedOptionsFromSettings();
    void applyWritingProfile();
    void refreshRefinementRows();
    void refreshOutputRows();
    void refreshFileList();
    void probeDuration(const QString &path);
    void startBatch();
    void setPhase(const QString &phase);
    void refreshQueue();
    void showResults();
    QString shownText(const TranscribeFileResult &result) const;
    TranscribeOptions options() const;

    ApplicationController *m_controller;
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

    // Results
    QLabel *m_resultsHeader;
    QLabel *m_summary;
    QWidget *m_variants;
    QAbstractButton *m_showRefined = nullptr;
    QFrame *m_resultsCard;

    // The running or last batch.
    QStringList m_batch;
    TranscribeOptions m_batchOptions;
    int m_current = -1;
    QList<TranscribeFileResult> m_batchResults;
};

} // namespace speecher
