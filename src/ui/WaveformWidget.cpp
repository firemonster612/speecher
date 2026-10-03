#include "ui/WaveformWidget.h"

#include "dictation/PopupGeometry.h"
#include "ui/settings/SettingsPageSupport.h"

#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QHideEvent>
#include <QPainter>
#include <QPalette>
#include <QShowEvent>
#include <QStyle>

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
// stands alone against a screen edge; Home's row keeps Speecher's original
// 48px take on it, and the popup uses the slimmer one every platform shares.
struct Geometry {
    int barCount;
    qreal barWidth;
    qreal barGap;
    qreal barDotHeight;
    // The standalone pill, and the low strip under the popup's transcript
    // line: just enough for the bars at full shout (barDotHeight * audioGain
    // * the 1.5 wave crest).
    int pillWidth;
    int pillHeight;
    int compactStripHeight;
};

// Wispr Flow's 2px bars scaled by the pill's height ratio (48/30), as rounded
// dots animated by the motion model in WaveformModel.h (shared with the
// Windows panel; the model comment there is the full story).
constexpr qreal standardScale = 48 / 30.0;
constexpr Geometry standardGeometry{waveform::barCount, 2.0 * standardScale, 2.0 * standardScale,
                                    2.0 * standardScale, 126, 48, 28};
// The popup's dots at Wispr Flow's own 2px, in a pill the dots and its
// rounded ends fill.
constexpr Geometry popupGeometry{popup::kBarCount, popup::kBarWidth, popup::kBarGap,
                                 popup::kBarDotHeight, 64, popup::kPillHeight,
                                 popup::kCompactStripHeight};

const Geometry &geometryFor(WaveformWidget::Size size)
{
    return size == WaveformWidget::Size::Popup ? popupGeometry : standardGeometry;
}

} // namespace

WaveformWidget::WaveformWidget(QWidget *parent, Size size)
    : QWidget(parent)
    , m_size(size)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    applyGeometry();
    m_clock.start();
    m_timer.setInterval(waveform::frameIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        if (m_mode == Mode::Frozen || m_mode == Mode::Paused) {
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
    const Geometry &g = geometryFor(m_size);
    const bool showsText = m_mode == Mode::Message || m_mode == Mode::Status;
    const int height = m_compact
        ? (showsText ? fontMetrics().height() + 6 : g.compactStripHeight)
        : std::max(g.pillHeight, fontMetrics().height() + 10);
    const int width = m_hugsInk ? contentWidth()
        : !m_message.isEmpty()  ? std::max(g.pillWidth, contentWidth() + 32)
                                : g.pillWidth;
    setFixedSize(width, height);
}

int WaveformWidget::iconSize() const
{
    return style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
}

int WaveformWidget::iconSpacing() const
{
    return style()->pixelMetric(QStyle::PM_LayoutHorizontalSpacing, nullptr, this);
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

void WaveformWidget::setHugsInk(bool hugs)
{
    if (m_hugsInk == hugs) {
        return;
    }
    m_hugsInk = hugs;
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
    if (mode != Mode::Message) {
        m_icon = QIcon();
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
    m_icon = QIcon();
    m_mode = m_message.isEmpty() ? Mode::Waveform : Mode::Status;
    m_level.restart(m_clock.elapsed());
    applyGeometry();
    update();
}

void WaveformWidget::setMessage(const QString &message, const QIcon &icon)
{
    m_message = message.simplified();
    m_icon = m_message.isEmpty() ? QIcon() : icon;
    m_mode = m_message.isEmpty() ? Mode::Waveform : Mode::Message;
    m_level.restart(m_clock.elapsed());
    applyGeometry();
    update();
}

int WaveformWidget::contentWidth() const
{
    if (m_mode == Mode::Message || m_mode == Mode::Status) {
        const int text = fontMetrics().horizontalAdvance(m_message);
        return m_icon.isNull() ? text : iconSize() + iconSpacing() + text;
    }
    const Geometry &g = geometryFor(m_size);
    return int(std::ceil(g.barCount * g.barWidth + (g.barCount - 1) * g.barGap));
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
    } else if (m_mode == Mode::Paused) {
        paintWaveform(painter, settings::neutralTextColor(p), true);
    } else {
        // Frozen keeps the bars at their last heights but drops them to the
        // 40% alpha Wispr Flow uses once the mic is no longer capturing.
        paintWaveform(painter, m_mode == Mode::Frozen ? withAlpha(bar, 102) : bar);
    }
}

void WaveformWidget::paintWaveform(QPainter &painter, const QColor &bar, bool flat)
{
    const Geometry &g = geometryFor(m_size);
    const qreal audioScale = m_level.audioScale();
    const qreal totalWidth = g.barCount * g.barWidth + (g.barCount - 1) * g.barGap;
    const qreal startX = (width() - totalWidth) / 2.0;
    const qreal barRadius = g.barWidth / 4.0;
    painter.setPen(Qt::NoPen);
    painter.setBrush(bar);
    for (int i = 0; i < g.barCount; ++i) {
        const qreal bulge = waveform::bulge(i, g.barCount);
        // Each bar trails its neighbour by one bar's share of the loop, so the
        // crest crosses the row exactly once per cycle however many bars there
        // are. At Wispr Flow's ten this is its own 0.1s delay.
        const qreal barPhase = m_wavePhase - qreal(i) / g.barCount;
        const qreal wave = waveform::waveMultiplier(barPhase - std::floor(barPhase));
        const qreal h = flat ? g.barDotHeight : g.barDotHeight * audioScale * bulge * wave;
        const qreal x = startX + i * (g.barWidth + g.barGap);
        // scaleY on the reference bar stretches its corners too, which tapers
        // the tips as the bar grows; the radius scales by the same factor.
        const qreal radiusY = barRadius * h / g.barDotHeight;
        painter.drawRoundedRect(QRectF(x, (height() - h) / 2.0, g.barWidth, h),
                                barRadius, radiusY);
    }
}

void WaveformWidget::paintStatus(QPainter &painter, const QColor &bar)
{
    QFont font = this->font();
    font.setWeight(QFont::Normal);
    painter.setFont(font);
    // Beside the popup's buttons the strip is exactly as wide as the label.
    const QRect textRect = m_hugsInk ? rect() : rect().adjusted(12, 0, -12, 0);

    // The palette's own secondary text colour, so the words stay readable
    // between passes of the highlight.
    painter.setPen(QApplication::palette().color(QPalette::PlaceholderText));
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
    if (m_icon.isNull()) {
        painter.drawText(rect().adjusted(12, 0, -12, 0), Qt::AlignCenter, m_message);
        return;
    }
    // Icon and text centred together as one line.
    const int icon = iconSize();
    const int left = (width() - contentWidth()) / 2;
    m_icon.paint(&painter, QRect(left, (height() - icon) / 2, icon, icon));
    const int textLeft = left + icon + iconSpacing();
    painter.drawText(QRect(textLeft, 0, width() - textLeft, height()),
                     Qt::AlignLeft | Qt::AlignVCenter, m_message);
}

} // namespace speecher
