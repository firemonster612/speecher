#pragma once

#include <QTimer>
#include <QString>
#include <QVector>
#include <QWidget>

namespace speecher {

class WaveformWidget : public QWidget {
    Q_OBJECT

public:
    enum class Mode { Waveform, Dots, Frozen, Message };

    explicit WaveformWidget(QWidget *parent = nullptr);

public slots:
    void setLevel(float level);
    void setMode(Mode mode);
    void setMessage(const QString &message);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void paintWaveform(QPainter &painter, const QColor &bar);
    void paintDots(QPainter &painter, const QColor &bar);
    void paintMessage(QPainter &painter, const QColor &bar);

    QTimer m_timer;
    QVector<float> m_bars;
    QString m_message;
    float m_targetLevel = 0.0f;
    float m_idlePhase = 0.0f;
    Mode m_mode = Mode::Waveform;
};

} // namespace speecher
