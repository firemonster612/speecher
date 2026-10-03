#pragma once

#include "ui/WaveformModel.h"

#include <QElapsedTimer>
#include <QIcon>
#include <QTimer>
#include <QString>
#include <QWidget>

class QFontMetrics;
class QHideEvent;
class QShowEvent;

namespace speecher {

class WaveformWidget : public QWidget {
    Q_OBJECT

public:
    // Paused: a still, flat row of dots in the colour scheme's caution colour.
    enum class Mode { Waveform, Frozen, Paused, Message, Status };

    explicit WaveformWidget(QWidget *parent = nullptr);
    void setBackgroundVisible(bool visible);
    // The width of what paintEvent actually draws in the current mode — the
    // bar row or the message text — as opposed to the fixed widget
    // bounds. The popup carves its contour around this.
    int contentWidth() const;
    // Low-strip geometry for the popup capsule, where the waveform sits under
    // the transcript line rather than standing alone. Off by default; the
    // Dictation page keeps the full-height pill.
    void setCompact(bool compact);

public slots:
    void setLevel(float level);
    void setMode(Mode mode);
    // A receipt, with the icon for its outcome before the text.
    void setMessage(const QString &message, const QIcon &icon = {});
    void setStatusText(const QString &text);

protected:
    void hideEvent(QHideEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void applyGeometry();
    int iconSize() const;
    int iconSpacing() const;
    void paintWaveform(QPainter &painter, const QColor &bar, bool flat = false);
    void paintMessage(QPainter &painter, const QColor &bar);
    void paintStatus(QPainter &painter, const QColor &bar);

    QTimer m_timer;
    // Drives the frame delta and the level model's windows. It keeps running
    // across Frozen spells; the wave phase, not the clock, is what freezes.
    QElapsedTimer m_clock;
    QString m_message;
    QIcon m_icon;
    waveform::LevelModel m_level;
    qint64 m_lastFrameMs = 0;
    float m_wavePhase = 0.0f;
    float m_idlePhase = 0.0f;
    Mode m_mode = Mode::Waveform;
    bool m_backgroundVisible = true;
    bool m_compact = false;
};

} // namespace speecher
