#pragma once

#include "transcribe/FileTranscriptionSession.h"
#include "transcribe/TranscribePresentation.h"
#include "ui/WaveformModel.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QTimer>

#include <functional>
#include <vector>

#include <windows.h>

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#pragma pop_macro("GetCurrentTime")

namespace speecher::win {

struct PaneHost;

// The Transcribe pane: pick audio files and options, watch the batch, read and
// export the transcripts. Its state outlives any one XAML tree, so the window
// can rebuild the pane (a theme change, a stage change) or close and reopen
// without losing the file list, a running batch or its results.
class TranscribePane : public QObject {
public:
    // rebuild re-renders the pane when it is the one on screen.
    TranscribePane(PaneHost &host, std::function<void()> rebuild);

    winrt::Microsoft::UI::Xaml::UIElement build();
    // Adds files to the setup list; paths that are not audio are skipped.
    void addFiles(const QStringList &paths);
    // Re-reads the option defaults from the user's settings while in setup.
    void enter();
    // The pane left the screen or the window closed: stop updating elements
    // nobody sees.
    void forgetElements();

private:
    enum class Stage { Setup, Processing, Results };

    void seedOptions();
    void applyWritingProfile();
    TranscribeOptions options() const;
    void startBatch();
    void retry(int index);
    void backToSetup();
    void appendSetup(const winrt::Microsoft::UI::Xaml::Controls::StackPanel &column);
    void appendProcessing(const winrt::Microsoft::UI::Xaml::Controls::StackPanel &column);
    void appendResults(const winrt::Microsoft::UI::Xaml::Controls::StackPanel &column);
    void refreshQueue();
    winrt::Microsoft::UI::Xaml::Controls::Button copyButton(const QString &label, const QString &text);
    void setProgress(qreal fraction);
    void setPhase(const QString &phase);
    void animateBars();

    winrt::fire_and_forget chooseFiles();
    winrt::fire_and_forget chooseFolder();
    winrt::fire_and_forget exportOne(QString audioPath, QString text);
    winrt::fire_and_forget exportAll();
    winrt::fire_and_forget dropFiles(winrt::Microsoft::UI::Xaml::DragEventArgs args);

    PaneHost &m_host;
    std::function<void()> m_rebuild;
    Stage m_stage = Stage::Setup;
    QStringList m_files;
    QString m_startError;

    // The setup choices, seeded from settings and never written back.
    QString m_speech;
    bool m_vocabulary = true;
    QString m_refiner;
    QString m_cleanup;
    QString m_profile;
    QString m_tone;
    TranscriptDestination m_destination = TranscriptDestination::BesideInput;
    QString m_folder;

    // The batch.
    QStringList m_batch;
    TranscribeOptions m_batchOptions;
    TranscribeBatchLabels m_batchLabels;
    QList<TranscribeFileResult> m_results;
    bool m_cancelled = false;
    // The result row a retry is running for, or -1.
    int m_retrying = -1;
    // Lengths of setup-list files (probed) and batch files (decoded).
    QHash<QString, qint64> m_durationsMs;
    int m_current = -1;
    QString m_currentPath;
    QString m_phase;
    qreal m_progress = 0;
    QVector<float> m_peaks;
    bool m_showRaw = false;
    QSet<int> m_expanded;
    QString m_resultsProblem;

    // Live elements of the processing stage, updated in place between rebuilds.
    winrt::Microsoft::UI::Xaml::Controls::ProgressBar m_progressBar{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock m_phaseText{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock m_percentText{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock m_headerText{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::StackPanel m_queue{nullptr};
    std::vector<winrt::Microsoft::UI::Xaml::Shapes::Rectangle> m_bars;
    QTimer m_barTimer;
    QElapsedTimer m_barClock;
    qint64 m_lastFrame = 0;
    float m_barPhase = 0;
    waveform::LevelModel m_level;
};

} // namespace speecher::win
