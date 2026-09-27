#pragma once

#include <QElapsedTimer>
#include <QProgressBar>
#include <QTimer>
#include <QVector>

namespace speecher {

// The Transcribe page's progress animation, approved as custom-painted
// content the way WaveformWidget is: the file's waveform, with a playhead
// sweeping across it as the audio is sent. A progress bar underneath, so
// assistive technology reads it as one, with its name and percentage.
// Palette colours only.
class TranscribeLoomWidget : public QProgressBar {
    Q_OBJECT

public:
    explicit TranscribeLoomWidget(QWidget *parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

public slots:
    // A new file: the playhead goes back to the start.
    void startFile();
    // The file's peak levels (0..1), once decoded; flat until then.
    void setPeaks(const QVector<float> &peaks);
    void setProgress(qreal fraction);
    // The file is done: the playhead runs to the end.
    void finishFile();

protected:
    void hideEvent(QHideEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void tick();

    QTimer m_timer;
    QElapsedTimer m_clock;
    QVector<float> m_peaks;
    // How unread audio breathes; new for every file, so no two runs alike.
    qreal m_breathPeriod = 300.0;
    qreal m_breathSpread = 0.7;
    qreal m_breathDepth = 0.1;
    qreal m_target = 0.0;
    // Eased toward m_target so progress that arrives in steps still glides.
    qreal m_shown = 0.0;
    bool m_landing = false;
    qreal m_landFrom = 0.0;
    qint64 m_landStarted = 0;
};

} // namespace speecher
