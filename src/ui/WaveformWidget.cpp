#include "ui/WaveformWidget.h"

#include <QApplication>
#include <QFont>
#include <QPainter>
#include <QPalette>

#include <algorithm>
#include <cmath>

namespace speecher {
namespace {

QColor withAlpha(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return color;
}

} // namespace

WaveformWidget::WaveformWidget(QWidget *parent)
    : QWidget(parent)
    , m_bars(10, 0.12f)
{
    setFixedSize(126, 48);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_timer.setInterval(24);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        if (m_mode == Mode::Frozen) {
            return;
        }
        m_idlePhase += 0.34f;
        for (int i = 0; i < m_bars.size(); ++i) {
            const float wave = 0.10f + 0.045f * std::sin(m_idlePhase + i * 0.7f);
            const float speech = m_targetLevel * (0.24f + 0.16f * std::sin(m_idlePhase * 1.4f + i * 1.23f) * std::sin(m_idlePhase * 0.61f + i));
            const float target = std::clamp(wave + speech, 0.08f, 1.0f);
            m_bars[i] = m_bars[i] * 0.42f + target * 0.58f;
        }
        m_targetLevel *= 0.68f;
        update();
    });
    m_timer.start();
}

void WaveformWidget::setLevel(float level)
{
    if (m_mode != Mode::Waveform) {
        return;
    }
    constexpr float noiseFloor = 0.14f;
    const float normalized = std::clamp((level - noiseFloor) / (1.0f - noiseFloor), 0.0f, 1.0f);
    const float boosted = std::clamp(std::pow(normalized, 1.18f) * 0.82f, 0.0f, 1.0f);
    m_targetLevel = std::max(m_targetLevel, boosted);
}

void WaveformWidget::setMode(Mode mode)
{
    if (m_mode == mode) {
        return;
    }
    m_mode = mode;
    if (mode != Mode::Message) {
        m_message.clear();
    }
    m_targetLevel = 0.0f;
    update();
}

void WaveformWidget::setMessage(const QString &message)
{
    m_message = message.simplified();
    m_mode = m_message.isEmpty() ? Mode::Waveform : Mode::Message;
    m_targetLevel = 0.0f;
    update();
}

void WaveformWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPalette p = QApplication::palette();
    const QColor pill = p.color(QPalette::Base);
    const QColor bar = p.color(QPalette::Text);
    const QColor stroke = withAlpha(p.color(QPalette::Mid), 150);
    painter.setPen(QPen(stroke, 1.2));
    painter.setBrush(pill);
    painter.drawRoundedRect(QRectF(rect()).adjusted(1.2, 1.2, -1.2, -1.2), (height() - 2.4) / 2.0, (height() - 2.4) / 2.0);

    if (m_mode == Mode::Message) {
        paintMessage(painter, bar);
    } else if (m_mode == Mode::Dots) {
        paintDots(painter, bar);
    } else {
        paintWaveform(painter, bar);
    }
}

void WaveformWidget::paintWaveform(QPainter &painter, const QColor &bar)
{
    const int bars = m_bars.size();
    const int gap = 6;
    const int width = 4;
    const int totalWidth = bars * width + (bars - 1) * gap;
    const int startX = (rect().width() - totalWidth) / 2;
    const int maxHeight = rect().height() - 8;
    for (int i = 0; i < bars; ++i) {
        const int h = std::clamp(int(8 + m_bars[i] * maxHeight), 10, maxHeight);
        const int x = startX + i * (width + gap);
        const int y = (height() - h) / 2;
        painter.setBrush(bar);
        painter.drawRoundedRect(QRect(x, y, width, h), width / 2.0, width / 2.0);
    }
}

void WaveformWidget::paintDots(QPainter &painter, const QColor &bar)
{
    const int radius = 4;
    const int gap = 8;
    const int totalWidth = radius * 6 + gap * 2;
    const int startX = (width() - totalWidth) / 2 + radius;
    const int centerY = height() / 2;
    const float phase = std::fmod(m_idlePhase * 0.45f, 3.0f);
    for (int i = 0; i < 3; ++i) {
        const float distance = std::abs(phase - i);
        const float wrappedDistance = std::min(distance, 3.0f - distance);
        const float alpha = 0.26f + 0.74f * std::clamp(1.0f - wrappedDistance, 0.0f, 1.0f);
        QColor dot = bar;
        dot.setAlphaF(alpha);
        painter.setBrush(dot);
        painter.drawEllipse(QPointF(startX + i * (radius * 2 + gap), centerY), radius, radius);
    }
}

void WaveformWidget::paintMessage(QPainter &painter, const QColor &bar)
{
    QFont font = painter.font();
    font.setPointSize(12);
    font.setWeight(QFont::Normal);
    painter.setFont(font);
    painter.setPen(bar);
    painter.drawText(rect().adjusted(12, 0, -12, 0), Qt::AlignCenter, m_message);
}

} // namespace speecher
