#pragma once

#include <QElapsedTimer>
#include <QTimer>
#include <QVector>
#include <QWidget>

namespace speecher {

// The Transcribe page's progress animation, approved as custom-painted
// content the way WaveformWidget is: the file's waveform hangs at the top, a
// playhead sweeps across it, and each bar it passes drops a mote into the
// page below, where word pills weave themselves in line by line. Palette
// colours only.
class TranscribeLoomWidget : public QWidget {
    Q_OBJECT

public:
    explicit TranscribeLoomWidget(QWidget *parent = nullptr);

    QSize sizeHint() const override;

public slots:
    // A new file: its peak levels (0..1) and a fresh page of words.
    void startFile(const QVector<float> &peaks, int seed);
    void setProgress(qreal fraction);

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
    };
    struct Mote {
        QPointF from;
        QPointF to;
        qreal t;
        qreal wobble;
    };

    void tick();

    QTimer m_timer;
    QElapsedTimer m_clock;
    QVector<float> m_peaks;
    QVector<Word> m_words;
    QVector<Mote> m_motes;
    qreal m_target = 0.0;
    // Eased toward m_target so progress that arrives in steps still glides.
    qreal m_shown = 0.0;
    quint32 m_random = 1;
};

} // namespace speecher
