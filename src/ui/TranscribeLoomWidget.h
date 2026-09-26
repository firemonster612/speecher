#pragma once

#include <QElapsedTimer>
#include <QTimer>
#include <QVector>
#include <QWidget>

namespace speecher {

// The Transcribe page's progress animation, approved as custom-painted
// content the way WaveformWidget is: the file's waveform hangs at the top, a
// playhead sweeps across it, and word pills write themselves into the page
// below, line by line. Each run is laid out and paced from its own seed.
// Palette colours only.
class TranscribeLoomWidget : public QWidget {
    Q_OBJECT

public:
    explicit TranscribeLoomWidget(QWidget *parent = nullptr);

    QSize sizeHint() const override;
    // True from finishFile() until the playhead has reached the end.
    bool isLanding() const;

public slots:
    // A new file: its peak levels (0..1) and a fresh page of words.
    void startFile(const QVector<float> &peaks, int seed);
    void setProgress(qreal fraction);
    // The file is done: the playhead runs to the end and the last words land,
    // then landed() follows, on the clock whether or not the widget is shown.
    void finishFile();

signals:
    void landed();

protected:
    void hideEvent(QHideEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    struct Word {
        int line;
        qreal x;
        qreal width;
        // The progress point where the word pops in.
        qreal at;
        // How long it takes to pop in, relative to the others.
        qreal pace;
    };
    // What varies from run to run besides the page layout.
    struct Look {
        qreal breathPeriod = 300.0;
        qreal breathSpread = 0.7;
        qreal breathDepth = 0.1;
        qreal drop = 6.0;
        qreal lineIndent = 0.0;
    };

    qreal random();
    void tick();
    void land();

    QTimer m_timer;
    QTimer m_landTimer;
    QElapsedTimer m_clock;
    QVector<float> m_peaks;
    QVector<Word> m_words;
    Look m_look;
    qreal m_target = 0.0;
    // Eased toward m_target so progress that arrives in steps still glides.
    qreal m_shown = 0.0;
    quint32 m_random = 1;
    bool m_landing = false;
    qreal m_landFrom = 0.0;
    qint64 m_landStarted = 0;
};

} // namespace speecher
