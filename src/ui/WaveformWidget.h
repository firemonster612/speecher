#pragma once

#include <QTimer>
#include <QString>
#include <QVector>
#include <QWidget>

class QHideEvent;
class QShowEvent;

namespace speecher {

class WaveformWidget : public QWidget {
    Q_OBJECT

public:
    enum class Mode { Waveform, Dots, Frozen, Message, Status };

    explicit WaveformWidget(QWidget *parent = nullptr);

public slots:
    void setLevel(float level);
    void setMode(Mode mode);
    void setMessage(const QString &message);
    void setStatusText(const QString &text);

protected:
    void hideEvent(QHideEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void paintWaveform(QPainter &painter, const QColor &bar);
    void paintDots(QPainter &painter, const QColor &bar);
    void paintMessage(QPainter &painter, const QColor &bar);
    void paintStatus(QPainter &painter, const QColor &bar);

    QTimer m_timer;
    QVector<float> m_bars;
    QString m_message;
    float m_targetLevel = 0.0f;
    float m_idlePhase = 0.0f;
    Mode m_mode = Mode::Waveform;
};

} // namespace speecher
