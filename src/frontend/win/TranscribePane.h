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

namespace speecher {

class ApplicationController;

namespace win {

struct PaneHost;

// The Transcribe pane: pick audio files and options, watch the batch, read and
// export the transcripts. One instance serves every window that shows it (the
// settings window's Transcribe pane and the standalone Transcribe window), and
// its state outlives any one XAML tree: a window can rebuild the pane or close
// and reopen without losing the file list, a running batch or its results, and
// a running batch shows in whichever window is open.
class TranscribePane : public QObject {
public:
    explicit TranscribePane(ApplicationController *controller);

    // A fresh tree for host's window, which from then on gets the pane's
    // rebuilds through host.refresh until forget(host). title heads the page;
    // empty for none.
    winrt::Microsoft::UI::Xaml::UIElement build(PaneHost &host, const QString &title);
    // Adds files to the setup list; paths that are not audio are skipped.
    void addFiles(const QStringList &paths);
    // The pane is coming on screen: while in setup and shown nowhere else,
    // re-reads the option defaults from the user's settings.
    void enter();
    // The pane left host's window, or the window closed: stop updating
    // elements nobody sees there.
    void forget(const PaneHost &host);

private:
    // One window's tree: the processing stage's elements, updated in place
    // between rebuilds.
    struct View {
        PaneHost *host = nullptr;
        winrt::Microsoft::UI::Xaml::Controls::ProgressBar progressBar{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock phaseText{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock percentText{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock headerText{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::StackPanel queue{nullptr};
        std::vector<winrt::Microsoft::UI::Xaml::Shapes::Rectangle> bars;
    };

    void rebuild();
    void seedOptions();
    void applyWritingProfile();
    TranscribeOptions options() const;
    void startBatch();
    void retry(int index);
    void backToSetup();
    void appendSetup(const winrt::Microsoft::UI::Xaml::Controls::StackPanel &column, PaneHost &host);
    void appendProcessing(const winrt::Microsoft::UI::Xaml::Controls::StackPanel &column, View &view);
    void appendResults(const winrt::Microsoft::UI::Xaml::Controls::StackPanel &column, PaneHost &host);
    void refreshQueue();
    void refreshQueue(const View &view);
    winrt::Microsoft::UI::Xaml::Controls::Button copyButton(const QString &label, const QString &text);
    void showProgress();
    void setPhase(TranscribePhase phase);
    void animateBars();
    void afterLanding(std::function<void()> event);
    void land();

    winrt::fire_and_forget chooseFiles(PaneHost &host);
    winrt::fire_and_forget chooseFolder(PaneHost &host);
    winrt::fire_and_forget exportOne(PaneHost &host, QString audioPath, QString text);
    winrt::fire_and_forget exportAll(PaneHost &host);
    winrt::fire_and_forget dropFiles(PaneHost &host, winrt::Microsoft::UI::Xaml::DragEventArgs args);

    ApplicationController *m_controller;
    std::vector<View> m_views;
    TranscribeStep m_step = TranscribeStep::Configure;
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
    TranscribePhase m_phase = TranscribePhase::Reading;
    // How long the current file has been in m_phase, which eases the
    // open-ended waits forward.
    QElapsedTimer m_phaseClock;
    qreal m_fractionSent = 0;
    // The current file is transcribed, refined and saved.
    bool m_fileFinished = false;
    // While a finished file shows at 100%, the session's later events wait here.
    bool m_landing = false;
    QList<std::function<void()>> m_afterLanding;
    QVector<float> m_peaks;
    bool m_showRaw = false;
    QSet<int> m_expanded;
    QString m_resultsProblem;

    QTimer m_barTimer;
    QElapsedTimer m_barClock;
    qint64 m_lastFrame = 0;
    float m_barPhase = 0;
    waveform::LevelModel m_level;
};

} // namespace win
} // namespace speecher
