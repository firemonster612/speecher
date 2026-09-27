#include "ui/TranscribeLoomWidget.h"

#include "ui/WaveformModel.h"

#include <QHideEvent>
#include <QPainter>
#include <QRandomGenerator>
#include <QShowEvent>

#include <algorithm>
#include <cmath>

namespace speecher {
namespace {

constexpr int kHeight = 112;
constexpr qreal kWaveTop = 16.0;
constexpr qreal kWaveHeight = 80.0;
constexpr qreal kSidePad = 18.0;
// Bars drawn before the decoder has read the file.
constexpr int kFlatBars = 120;
// How far behind the playhead a consumed bar fades out, in pixels.
constexpr qreal kFadeDistance = 60.0;
// How long the playhead takes to run to the end once a file is done, within
// the landing hold before the next file replaces it. Measured on the clock,
// so a minimized window lands on time.
constexpr qint64 kLandRunMs = 350;

QColor withAlpha(QColor color, qreal alpha)
{
    color.setAlphaF(float(std::clamp(alpha, 0.0, 1.0)));
    return color;
}

qreal randomBetween(qreal low, qreal high)
{
    return low + QRandomGenerator::global()->generateDouble() * (high - low);
}

} // namespace

TranscribeLoomWidget::TranscribeLoomWidget(QWidget *parent)
    : QProgressBar(parent)
{
    setAccessibleName(QStringLiteral("Transcription progress"));
    setTextVisible(false);
    setRange(0, 100);
    setValue(0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_clock.start();
    m_timer.setInterval(waveform::frameIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &TranscribeLoomWidget::tick);
}

QSize TranscribeLoomWidget::sizeHint() const
{
    return {480, kHeight};
}

QSize TranscribeLoomWidget::minimumSizeHint() const
{
    return {int(kSidePad * 4), kHeight};
}

void TranscribeLoomWidget::startFile()
{
    m_peaks.clear();
    m_target = 0.0;
    m_shown = 0.0;
    m_landing = false;
    m_breathPeriod = randomBetween(220.0, 420.0);
    m_breathSpread = randomBetween(0.4, 1.3);
    m_breathDepth = randomBetween(0.06, 0.16);
    setValue(0);
    update();
}

void TranscribeLoomWidget::setPeaks(const QVector<float> &peaks)
{
    m_peaks = peaks;
    update();
}

void TranscribeLoomWidget::setProgress(qreal fraction)
{
    m_target = std::clamp(fraction, 0.0, 1.0);
    setValue(int(m_target * 100));
}

void TranscribeLoomWidget::finishFile()
{
    m_target = 1.0;
    setValue(100);
    m_landing = true;
    m_landFrom = m_shown;
    m_landStarted = m_clock.elapsed();
}

void TranscribeLoomWidget::showEvent(QShowEvent *event)
{
    m_timer.start();
    QProgressBar::showEvent(event);
}

void TranscribeLoomWidget::hideEvent(QHideEvent *event)
{
    m_timer.stop();
    QProgressBar::hideEvent(event);
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
    const qreal now = m_clock.elapsed();
    const qreal innerWidth = width() - kSidePad * 2;
    const qreal headX = kSidePad + innerWidth * m_shown;
    const qreal waveMid = kWaveTop + kWaveHeight / 2;

    // The file's waveform: bars behind the playhead have given up their sound
    // and shrink away; unread audio breathes, at this file's own pace.
    const QVector<float> flat(kFlatBars, 0.04f);
    const QVector<float> &levels = m_peaks.isEmpty() ? flat : m_peaks;
    const int count = levels.size();
    for (int i = 0; i < count; ++i) {
        const qreal x = kSidePad + (count > 1 ? qreal(i) / (count - 1) : 0.0) * innerWidth;
        qreal height = std::max(0.04f, levels.at(i)) * kWaveHeight;
        QColor color;
        if (x <= headX) {
            const qreal fade = std::max(0.0, 1 - (headX - x) / kFadeDistance);
            height *= 0.25 + 0.75 * fade;
            color = withAlpha(accent, 0.25 + 0.75 * fade);
        } else {
            height *= 1 - m_breathDepth + m_breathDepth * std::sin(now / m_breathPeriod + i * m_breathSpread);
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
}

} // namespace speecher
