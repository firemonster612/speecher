#include "ui/TranscribeLoomWidget.h"

#include "ui/WaveformModel.h"

#include <QHideEvent>
#include <QPainter>
#include <QPainterPath>
#include <QShowEvent>

#include <algorithm>
#include <cmath>

namespace speecher {
namespace {

// Geometry, in the mockup's proportions.
constexpr int kHeight = 280;
constexpr qreal kWaveTop = 16.0;
constexpr qreal kWaveHeight = 80.0;
constexpr qreal kSidePad = 18.0;
constexpr qreal kPageGap = 40.0;
constexpr qreal kLineGap = 23.0;
constexpr qreal kPillHeight = 9.0;
constexpr int kLines = 6;
// How much of the progress bar one word takes to pop in.
constexpr qreal kPopWindow = 0.045;
// How far behind the playhead a consumed bar fades out, in pixels.
constexpr qreal kFadeDistance = 60.0;

qreal easeOutBack(qreal t)
{
    constexpr qreal c = 1.70158;
    return 1 + (c + 1) * std::pow(t - 1, 3) + c * std::pow(t - 1, 2);
}

QColor withAlpha(QColor color, qreal alpha)
{
    color.setAlphaF(float(std::clamp(alpha, 0.0, 1.0)));
    return color;
}

} // namespace

TranscribeLoomWidget::TranscribeLoomWidget(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(kHeight);
    m_clock.start();
    m_timer.setInterval(waveform::frameIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &TranscribeLoomWidget::tick);
}

QSize TranscribeLoomWidget::sizeHint() const
{
    return {480, kHeight};
}

void TranscribeLoomWidget::startFile(const QVector<float> &peaks, int seed)
{
    m_peaks = peaks;
    m_motes.clear();
    m_words.clear();
    m_target = 0.0;
    m_shown = 0.0;
    m_random = quint32(seed) * 2654435761u + 1;
    // Word pills laid out into lines; each gets the progress point where it
    // pops in, spread evenly across the file.
    const auto next = [this] {
        m_random = m_random * 1664525u + 1013904223u;
        return (m_random >> 8) / qreal(1 << 24);
    };
    int line = 0;
    qreal x = 0.0;
    while (line < kLines) {
        const qreal width = 0.035 + next() * 0.085;
        if (x + width > 1.0) {
            ++line;
            x = 0.0;
            continue;
        }
        m_words.append({line, x, width, 0.0});
        x += width + 0.018;
    }
    for (int i = 0; i < m_words.size(); ++i) {
        m_words[i].at = 0.06 + 0.88 * qreal(i) / m_words.size();
    }
    update();
}

void TranscribeLoomWidget::setProgress(qreal fraction)
{
    m_target = std::clamp(fraction, 0.0, 1.0);
}

void TranscribeLoomWidget::showEvent(QShowEvent *event)
{
    m_timer.start();
    QWidget::showEvent(event);
}

void TranscribeLoomWidget::hideEvent(QHideEvent *event)
{
    m_timer.stop();
    QWidget::hideEvent(event);
}

void TranscribeLoomWidget::tick()
{
    m_shown += (m_target - m_shown) * 0.12;
    const qreal innerWidth = width() - kSidePad * 2;
    const qreal headX = kSidePad + innerWidth * m_shown;
    const qreal pageTop = kWaveTop + kWaveHeight + kPageGap;
    // Sound falls from the playhead toward the next word still to land.
    const auto landing = std::find_if(m_words.cbegin(), m_words.cend(),
                                      [this](const Word &word) { return word.at > m_shown; });
    const auto next = [this] {
        m_random = m_random * 1664525u + 1013904223u;
        return (m_random >> 8) / qreal(1 << 24);
    };
    if (landing != m_words.cend() && m_shown < m_target + 0.001 && m_target < 1.0 && next() < 0.5) {
        m_motes.append({QPointF(headX, kWaveTop + kWaveHeight / 2 + (next() - 0.5) * kWaveHeight * 0.6),
                        QPointF(kSidePad + (landing->x + landing->width / 2) * innerWidth,
                                pageTop + landing->line * kLineGap),
                        0.0,
                        next() * 2 * M_PI});
    }
    for (Mote &mote : m_motes) {
        mote.t = std::min(1.0, mote.t + 0.028);
    }
    m_motes.erase(std::remove_if(m_motes.begin(), m_motes.end(),
                                 [](const Mote &mote) { return mote.t >= 1.0; }),
                  m_motes.end());
    update();
}

void TranscribeLoomWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPalette &colors = palette();
    const QColor accent = colors.color(QPalette::Highlight);
    const QColor dim = colors.color(QPalette::PlaceholderText);
    const QColor ink = colors.color(QPalette::Text);
    const qreal now = m_clock.elapsed();
    const qreal innerWidth = width() - kSidePad * 2;
    const qreal headX = kSidePad + innerWidth * m_shown;
    const qreal waveMid = kWaveTop + kWaveHeight / 2;

    // The file's waveform: bars behind the playhead have given up their sound
    // and shrink away; unread audio breathes gently.
    const int count = m_peaks.size();
    for (int i = 0; i < count; ++i) {
        const qreal x = kSidePad + (count > 1 ? qreal(i) / (count - 1) : 0.0) * innerWidth;
        qreal height = std::max(0.04f, m_peaks.at(i)) * kWaveHeight;
        QColor color;
        if (x <= headX) {
            const qreal fade = std::max(0.0, 1 - (headX - x) / kFadeDistance);
            height *= 0.25 + 0.75 * fade;
            color = withAlpha(accent, 0.25 + 0.75 * fade);
        } else {
            height *= 0.9 + 0.1 * std::sin(now / 300 + i * 0.7);
            color = withAlpha(dim, 0.55);
        }
        painter.setPen(QPen(color, 2.2, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(x, waveMid - height / 2), QPointF(x, waveMid + height / 2));
    }

    // The playhead, brightest at the waveform's middle.
    QLinearGradient glow(headX, kWaveTop - 6, headX, kWaveTop + kWaveHeight + 6);
    glow.setColorAt(0.0, withAlpha(accent, 0.0));
    glow.setColorAt(0.5, accent);
    glow.setColorAt(1.0, withAlpha(accent, 0.0));
    painter.setPen(QPen(QBrush(glow), 2));
    painter.drawLine(QPointF(headX, kWaveTop - 6), QPointF(headX, kWaveTop + kWaveHeight + 6));

    // Words weaving into the page: fresh ones glow in the accent, then
    // settle into ink.
    const qreal pageTop = kWaveTop + kWaveHeight + kPageGap;
    painter.setPen(Qt::NoPen);
    const Word *newest = nullptr;
    for (const Word &word : std::as_const(m_words)) {
        const qreal local = (m_shown - word.at) / kPopWindow;
        if (local <= 0) {
            continue;
        }
        newest = &word;
        const qreal k = std::min(1.0, local);
        const qreal eased = easeOutBack(k);
        const qreal y = pageTop + word.line * kLineGap - kPillHeight / 2 + (1 - eased) * 6;
        painter.setBrush(k < 1 ? withAlpha(accent, std::min(1.0, k * 1.4)) : withAlpha(ink, 0.82));
        painter.drawRoundedRect(QRectF(kSidePad + word.x * innerWidth, y,
                                       std::max(2.0, word.width * innerWidth * eased), kPillHeight),
                                4.5, 4.5);
    }

    // Motes arcing down from the playhead to the word about to land.
    for (const Mote &mote : std::as_const(m_motes)) {
        const qreal k = mote.t;
        const qreal arc = std::sin(k * M_PI) * 26;
        const QPointF at(mote.from.x() + (mote.to.x() - mote.from.x()) * k
                             + std::sin(mote.wobble + k * 6) * 5 * (1 - k),
                         mote.from.y() + (mote.to.y() - mote.from.y()) * k * k - arc * (1 - k) * 0.3);
        painter.setBrush(withAlpha(accent, 0.9 * (1 - std::abs(k - 0.5) * 0.6)));
        const qreal radius = 2.6 * (1 - k * 0.5);
        painter.drawEllipse(at, radius, radius);
    }

    // A caret blinking after the newest word.
    if (newest && m_shown < 0.999 && int(now / 450) % 2 == 0) {
        painter.setBrush(accent);
        painter.drawRect(QRectF(kSidePad + (newest->x + newest->width) * innerWidth + 5,
                                pageTop + newest->line * kLineGap - 8, 2, 16));
    }
}

} // namespace speecher
