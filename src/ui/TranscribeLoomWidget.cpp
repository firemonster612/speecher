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
// How long the playhead takes to run to the end once a file is done, and how
// long the finished page then stays up before the next file replaces it.
// Both are measured on the clock, so a minimized window lands on time.
constexpr qint64 kLandRunMs = 350;
constexpr qint64 kLandedHoldMs = 450;

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
    m_landTimer.setSingleShot(true);
    connect(&m_landTimer, &QTimer::timeout, this, &TranscribeLoomWidget::land);
}

QSize TranscribeLoomWidget::sizeHint() const
{
    return {480, kHeight};
}

bool TranscribeLoomWidget::isLanding() const
{
    return m_landing;
}

qreal TranscribeLoomWidget::random()
{
    m_random = m_random * 1664525u + 1013904223u;
    return (m_random >> 8) / qreal(1 << 24);
}

void TranscribeLoomWidget::startFile(const QVector<float> &peaks, int seed)
{
    m_peaks = peaks;
    m_words.clear();
    m_target = 0.0;
    m_shown = 0.0;
    m_landing = false;
    m_landTimer.stop();
    // The seed differs on every run, so each file lays out its own page and
    // breathes and settles in its own way.
    m_random = quint32(seed) * 2654435761u + 1;
    m_look.breathPeriod = 220.0 + random() * 200.0;
    m_look.breathSpread = 0.4 + random() * 0.9;
    m_look.breathDepth = 0.06 + random() * 0.1;
    m_look.drop = 3.0 + random() * 7.0;
    m_look.lineIndent = random() * 0.12;
    // Word pills laid out into lines, each line a little ragged on the left
    // and ending short of the margin by its own amount; each word gets the
    // progress point where it pops in, spread across the file with some jitter.
    int line = 0;
    qreal x = m_look.lineIndent * random();
    qreal lineEnd = 0.8 + random() * 0.2;
    while (line < kLines) {
        const qreal width = 0.025 + random() * random() * 0.14;
        if (x + width > lineEnd) {
            ++line;
            x = m_look.lineIndent * random();
            lineEnd = line == kLines - 1 ? 0.3 + random() * 0.5 : 0.8 + random() * 0.2;
            continue;
        }
        m_words.append({line, x, width, 0.0, 0.8 + random() * 0.4});
        x += width + 0.012 + random() * 0.014;
    }
    for (int i = 0; i < m_words.size(); ++i) {
        const qreal jitter = (random() - 0.5) * 0.6 / m_words.size();
        m_words[i].at = std::clamp(0.06 + 0.88 * qreal(i) / m_words.size() + jitter, 0.05, 0.95);
    }
    update();
}

void TranscribeLoomWidget::setProgress(qreal fraction)
{
    m_target = std::clamp(fraction, 0.0, 1.0);
}

void TranscribeLoomWidget::finishFile()
{
    m_target = 1.0;
    m_landing = true;
    m_landFrom = m_shown;
    m_landStarted = m_clock.elapsed();
    m_landTimer.start(int(kLandRunMs + kLandedHoldMs));
}

void TranscribeLoomWidget::land()
{
    m_landTimer.stop();
    m_shown = 1.0;
    m_landing = false;
    emit landed();
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
    if (m_landing) {
        // Run the playhead to the end in a fixed time from wherever it was.
        const qreal t = std::min(1.0, qreal(m_clock.elapsed() - m_landStarted) / kLandRunMs);
        m_shown = m_landFrom + (1.0 - m_landFrom) * (1 - std::pow(1 - t, 3));
    } else {
        m_shown += (m_target - m_shown) * 0.12;
    }
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
    // and shrink away; unread audio breathes, at this run's own pace.
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
            height *= 1 - m_look.breathDepth
                      + m_look.breathDepth * std::sin(now / m_look.breathPeriod + i * m_look.breathSpread);
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

    // Words writing themselves into the page: fresh ones drop in from above
    // in the accent and grow to their length at their own speed, then settle
    // into ink.
    const qreal pageTop = kWaveTop + kWaveHeight + kPageGap;
    painter.setPen(Qt::NoPen);
    const Word *newest = nullptr;
    for (const Word &word : std::as_const(m_words)) {
        const qreal local = (m_shown - word.at) / (kPopWindow * word.pace);
        if (local <= 0) {
            continue;
        }
        newest = &word;
        const qreal k = std::min(1.0, local);
        const qreal eased = easeOutBack(k);
        const qreal y = pageTop + word.line * kLineGap - kPillHeight / 2 - (1 - eased) * m_look.drop;
        painter.setBrush(k < 1 ? withAlpha(accent, std::min(1.0, k * 1.4)) : withAlpha(ink, 0.82));
        painter.drawRoundedRect(QRectF(kSidePad + word.x * innerWidth, y,
                                       std::max(2.0, word.width * innerWidth * eased), kPillHeight),
                                4.5, 4.5);
    }

    // A caret blinking after the newest word.
    if (newest && m_shown < 0.999 && int(now / 450) % 2 == 0) {
        painter.setBrush(accent);
        painter.drawRect(QRectF(kSidePad + (newest->x + newest->width) * innerWidth + 5,
                                pageTop + newest->line * kLineGap - 8, 2, 16));
    }
}

} // namespace speecher
