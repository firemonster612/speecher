#include "ui/WaveformWidget.h"

#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QHideEvent>
#include <QPainter>
#include <QPalette>
#include <QShowEvent>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace speecher {
namespace {

QColor withAlpha(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return color;
}

// One pill for every state, so it never changes size between listening, the
// delivery receipt and the status shimmer. Wispr Flow's own pill is 50x30 and
// stands alone against a screen edge; Speecher's sits directly above the
// transcript pill, so it takes that pill's size instead.
constexpr int pillWidth = 126;
constexpr int pillHeight = 48;
// Bar geometry is Wispr Flow's 2px scaled by the pill's height ratio (48/30).
constexpr qreal referencePillHeight = 30.0;
constexpr qreal pillScale = pillHeight / referencePillHeight;

// The waveform is a port of Wispr Flow's status-bar bars: rounded dots,
// 2x2px before pillScale, animated by the motion model in WaveformModel.h
// (shared with the Windows panel; the model comment there is the full story).
// The compact strip under the popup's transcript line: just enough for the
// bars at full shout (barDotHeight * audioGain * the 1.5 wave crest = 24px).
constexpr int compactStripHeight = 28;
constexpr qreal barWidth = 2.0 * pillScale;
constexpr qreal barGap = 2.0 * pillScale;
constexpr qreal barDotHeight = 2.0 * pillScale;
constexpr qreal barRadius = 0.5 * pillScale;
constexpr int barCount = waveform::barCount;

} // namespace

WaveformWidget::WaveformWidget(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    applyGeometry();
    m_clock.start();
    m_timer.setInterval(waveform::frameIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        if (m_mode == Mode::Frozen) {
            return;
        }
        const qint64 now = m_clock.elapsed();
        // Clamped so a Frozen spell or a missed tick cannot jump the bars.
        const float dt = std::min(qreal(now - m_lastFrameMs) / 1000.0, 0.1);
        m_lastFrameMs = now;
        m_idlePhase += 14.2f * dt;
        // One cycle per second, accumulated rather than read from the clock in
        // paintWaveform: ticks stop while frozen, so the bars hold their last
        // heights however often the widget repaints, and resume without a jump.
        m_wavePhase = std::fmod(m_wavePhase + dt, 1.0f);
        if (m_mode == Mode::Waveform) {
            m_level.advance(now);
        }
        update();
    });
}

void WaveformWidget::applyGeometry()
{
    // The height follows the desktop's font where that is taller, so a large
    // font cannot clip the receipt; a long message widens the pill. Compact
    // trades the standalone pill's air for a low strip, keeping the font's
    // height only when the strip carries text (the status shimmer).
    const bool showsText = m_mode == Mode::Message || m_mode == Mode::Status;
    const int height = m_compact
        ? (showsText ? fontMetrics().height() + 6 : compactStripHeight)
        : std::max(pillHeight, fontMetrics().height() + 10);
    const int width = m_message.isEmpty()
        ? pillWidth
        : std::max(pillWidth, fontMetrics().horizontalAdvance(m_message) + 32);
    setFixedSize(width, height);
}

void WaveformWidget::setCompact(bool compact)
{
    if (m_compact == compact) {
        return;
    }
    m_compact = compact;
    applyGeometry();
    update();
}

void WaveformWidget::hideEvent(QHideEvent *event)
{
    m_timer.stop();
    // Levels keep arriving while the popup shows an error, and the windowing
    // runs on the frame timer; without this the first tick after the next show
    // would average the whole hidden stretch into one window.
    m_level.restart(m_clock.elapsed());
    QWidget::hideEvent(event);
}

void WaveformWidget::showEvent(QShowEvent *event)
{
    m_lastFrameMs = m_clock.elapsed();
    m_level.restart(m_lastFrameMs);
    m_timer.start();
    QWidget::showEvent(event);
}

void WaveformWidget::setLevel(float level)
{
    if (m_mode != Mode::Waveform) {
        return;
    }
    m_level.addChunk(level);
}

void WaveformWidget::setMode(Mode mode)
{
    if (m_mode == mode) {
        return;
    }
    m_mode = mode;
    if (mode != Mode::Message && mode != Mode::Status) {
        m_message.clear();
    }
    // Frozen holds the bars where they were; every other mode change starts a
    // fresh capture.
    if (mode != Mode::Frozen) {
        m_level.restart(m_clock.elapsed());
    }
    applyGeometry();
    update();
}

void WaveformWidget::setStatusText(const QString &text)
{
    m_message = text.simplified();
    m_mode = m_message.isEmpty() ? Mode::Waveform : Mode::Status;
    m_level.restart(m_clock.elapsed());
    applyGeometry();
    update();
}

void WaveformWidget::setMessage(const QString &message)
{
    m_message = message.simplified();
    m_mode = m_message.isEmpty() ? Mode::Waveform : Mode::Message;
    m_level.restart(m_clock.elapsed());
    applyGeometry();
    update();
}

int WaveformWidget::contentWidth() const
{
    if (m_mode == Mode::Message || m_mode == Mode::Status) {
        return fontMetrics().horizontalAdvance(m_message);
    }
    return int(std::ceil(barCount * barWidth + (barCount - 1) * barGap));
}

void WaveformWidget::setBackgroundVisible(bool visible)
{
    m_backgroundVisible = visible;
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
    // One device pixel, centered on the device-pixel grid: on fractionally
    // scaled displays a logical 1px+ stroke lands between device pixels and
    // renders as a soft 2px blur ring.
    const qreal dpr = devicePixelRatioF() > 0 ? devicePixelRatioF() : 1.0;
    const qreal penWidth = 1.0 / dpr;
    const qreal inset = penWidth / 2.0;
    painter.setPen(QPen(stroke, penWidth));
    painter.setBrush(pill);
    const QRectF pillRect = QRectF(rect()).adjusted(inset, inset, -inset, -inset);
    if (m_backgroundVisible) {
        painter.drawRoundedRect(pillRect, pillRect.height() / 2.0, pillRect.height() / 2.0);
    }

    if (m_mode == Mode::Message) {
        paintMessage(painter, bar);
    } else if (m_mode == Mode::Status) {
        paintStatus(painter, bar);
    } else {
        // Frozen keeps the bars at their last heights but drops them to the
        // 40% alpha Wispr Flow uses once the mic is no longer capturing.
        paintWaveform(painter, m_mode == Mode::Frozen ? withAlpha(bar, 102) : bar);
    }
}

void WaveformWidget::paintWaveform(QPainter &painter, const QColor &bar)
{
    const qreal audioScale = m_level.audioScale();
    const qreal totalWidth = barCount * barWidth + (barCount - 1) * barGap;
    const qreal startX = (width() - totalWidth) / 2.0;
    painter.setPen(Qt::NoPen);
    painter.setBrush(bar);
    for (int i = 0; i < barCount; ++i) {
        const qreal bulge = waveform::bulge(i);
        // Each bar trails its neighbour by one bar's share of the loop, so the
        // crest crosses the row exactly once per cycle however many bars there
        // are. At Wispr Flow's ten this is its own 0.1s delay.
        const qreal barPhase = m_wavePhase - qreal(i) / barCount;
        const qreal wave = waveform::waveMultiplier(barPhase - std::floor(barPhase));
        const qreal h = barDotHeight * audioScale * bulge * wave;
        const qreal x = startX + i * (barWidth + barGap);
        // scaleY on the reference bar stretches its corners too, which tapers
        // the tips as the bar grows; the radius scales by the same factor.
        const qreal radiusY = barRadius * h / barDotHeight;
        painter.drawRoundedRect(QRectF(x, (height() - h) / 2.0, barWidth, h),
                                barRadius, radiusY);
    }
}

void WaveformWidget::paintStatus(QPainter &painter, const QColor &bar)
{
    QFont font = this->font();
    font.setWeight(QFont::Normal);
    painter.setFont(font);
    const QRect textRect = rect().adjusted(12, 0, -12, 0);

    QColor dim = bar;
    dim.setAlphaF(0.38f);
    painter.setPen(dim);
    painter.drawText(textRect, Qt::AlignCenter, m_message);

    // A soft highlight band sweeps the text left to right and loops, so the
    // word reads as "in progress" without a spinner. The band travels one
    // widget width plus its own width per loop; m_idlePhase advances ~14/s.
    const qreal band = width() * 0.55;
    const qreal travel = width() + band * 2.0;
    const qreal pos = std::fmod(qreal(m_idlePhase) * 11.0, travel) - band;
    QLinearGradient sweep(pos, 0, pos + band, 0);
    QColor clear = bar;
    clear.setAlphaF(0.0f);
    sweep.setColorAt(0.0, clear);
    sweep.setColorAt(0.5, bar);
    sweep.setColorAt(1.0, clear);
    painter.setPen(QPen(QBrush(sweep), 0));
    painter.drawText(textRect, Qt::AlignCenter, m_message);
}

void WaveformWidget::paintMessage(QPainter &painter, const QColor &bar)
{
    // The widget's own font is the application font, so the message follows
    // the desktop's font and size choices.
    QFont font = this->font();
    font.setWeight(QFont::Normal);
    painter.setFont(font);
    painter.setPen(bar);
    painter.drawText(rect().adjusted(12, 0, -12, 0), Qt::AlignCenter, m_message);
}

} // namespace speecher
