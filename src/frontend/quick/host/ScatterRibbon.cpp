#include "ScatterRibbon.h"

#include <QPainter>
#include <QQuickWindow>
#include <QRandomGenerator>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace speecher {

namespace {

// Five strands of evenly spaced points, so the band and its twist read crisply.
constexpr int kStrands = 5;
constexpr int kPerStrand = 104;
// The figure's ends in from the strip's, leaving room for freed points and the glow.
constexpr qreal kInset = 6;
// Ring depth and band half-width (px) before the tilt.
constexpr qreal kDepth = 12;
constexpr qreal kBand = 3.8;
// How far a freed point may stray from its home (px).
constexpr qreal kDriftX = 14;
constexpr qreal kDriftY = 5;
// A level under this is the microphone's own noise.
constexpr qreal kSilent = 0.02;
// How long a moving point's afterglow lasts (s).
constexpr qreal kAfterglow = 0.06;
// The glow is the trail shrunk this many px to one and stretched back,
// which blurs it by about 1.6 px.
constexpr qreal kGlowShrink = 3;
// A stalled frame resumes the figure rather than leaping it on.
constexpr qreal kLongestStep = 0.05;

// Eases value toward target, most of the way in about ms, at any frame rate.
qreal follow(qreal value, qreal target, qreal ms, qreal dt)
{
    return value + (target - value) * (1 - std::exp(-dt / (ms / 3000)));
}

// A divergence-free flow from a stream function, so freed points travel in
// streams instead of spraying. Its velocity is about unit size.
QPointF curl(qreal x, qreal y, qreal t)
{
    const qreal a = 1.0 / 16, b = 1.0 / 6;
    const qreal X = x * a + t * 0.7, Y = y * b - t * 0.4, Z = x * a * 1.7 - y * b * 1.2 + t * 1.1;
    const qreal dx = a * std::cos(X) * std::cos(Y) + 0.6 * 1.7 * a * std::cos(Z);
    const qreal dy = -b * std::sin(X) * std::sin(Y) - 0.6 * 1.2 * b * std::cos(Z);
    return {dy / b, -dx / b};
}

QColor mix(const QColor &from, const QColor &to, qreal amount)
{
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * amount,
                            from.greenF() + (to.greenF() - from.greenF()) * amount,
                            from.blueF() + (to.blueF() - from.blueF()) * amount);
}

// Adds a round dot of colour (premultiplied) to image, centred on centre
// with radius r, both in device px, its edge antialiased over a pixel.
// QPainter's antialiased ellipse costs ten times as much at this size, and
// the ribbon draws hundreds a frame.
void splat(QImage &image, QPointF centre, qreal r, QRgb colour)
{
    const int left = std::max(0, int(centre.x() - r - 0.5)), right = std::min(image.width() - 1, int(centre.x() + r + 0.5));
    const int top = std::max(0, int(centre.y() - r - 0.5)), bottom = std::min(image.height() - 1, int(centre.y() + r + 0.5));
    for (int y = top; y <= bottom; ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = left; x <= right; ++x) {
            const qreal coverage = std::clamp(r + 0.5 - std::hypot(x + 0.5 - centre.x(), y + 0.5 - centre.y()), 0.0, 1.0);
            const uint source = uint(coverage * 255 + 0.5);
            if (source == 0) {
                continue;
            }
            // Source over: the dot at its coverage, over what is there.
            const QRgb under = line[x];
            const uint keep = 255 - qAlpha(colour) * source / 255;
            line[x] = qRgba(qRed(colour) * source / 255 + qRed(under) * keep / 255,
                            qGreen(colour) * source / 255 + qGreen(under) * keep / 255,
                            qBlue(colour) * source / 255 + qBlue(under) * keep / 255,
                            qAlpha(colour) * source / 255 + qAlpha(under) * keep / 255);
        }
    }
}

} // namespace

RibbonFigure::RibbonFigure()
{
    // Seeded, so the same voice frees the same points every time.
    QRandomGenerator random(4242);
    m_points.reserve(kStrands * kPerStrand);
    for (int strand = 0; strand < kStrands; ++strand) {
        for (int i = 0; i < kPerStrand; ++i) {
            Point point;
            point.u = (i + qreal(strand) / kStrands) / kPerStrand * 2 * std::numbers::pi;
            point.v = qreal(strand) / (kStrands - 1) * 2 - 1;
            point.threshold = 0.5 * random.generateDouble();
            m_points.append(point);
        }
    }
}

void RibbonFigure::hear(qreal level, bool listening)
{
    m_level = std::clamp(level, 0.0, 1.0);
    const qreal shown = m_level < kSilent ? 0 : std::sqrt(m_level);
    // A word start: the level jumps well above where it has been.
    const qreal onset = std::max(0.0, shown - m_smoothed);
    if (listening && onset > 0.08) {
        m_burst = std::min(1.2, m_burst + onset * 3);
    }
}

void RibbonFigure::step(qreal dt, QSizeF strip, bool listening, qreal movement)
{
    const qreal left = kInset, right = strip.width() - kInset;
    const qreal centreX = (left + right) / 2, centreY = strip.height() / 2;
    const qreal radius = (right - left) / 2 - 3;
    // Too narrow to hold the loop, as before the popup lays it out.
    if (radius <= 0) {
        return;
    }

    // Where the level has been, eased frame by frame, so a word start after
    // a quiet stretch counts from the quiet even when no new level arrived.
    const qreal shown = m_level < kSilent ? 0 : std::sqrt(m_level);
    m_smoothed = follow(m_smoothed, shown, shown > m_smoothed ? 120 : 480, dt);
    // raw^0.7 keeps the voice's range.
    const qreal voice = listening && m_level >= kSilent ? std::pow(m_level, 0.7) : 0;
    m_envelope = follow(m_envelope, voice, voice > m_envelope ? 30 : 120, dt);
    m_dim = follow(m_dim, listening ? 0 : 1, 280, dt);
    m_burst *= std::exp(-dt / 0.25);
    // The twist crawls round the loop and the loop rocks, both slowly at
    // rest and faster with the voice; paused, it holds still.
    const qreal run = (1 - m_dim) * movement;
    m_twist += dt * (0.35 + 1.4 * m_envelope) * run;
    m_rock += dt * (0.22 + 0.5 * m_envelope) * run;
    m_flowTime += dt * (0.3 + 1.5 * m_envelope);
    const qreal tilt = 0.72 + 0.14 * std::sin(m_rock), cosTilt = std::cos(tilt), sinTilt = std::sin(tilt);
    const qreal reach = std::min(1.6, m_envelope * (1 + 0.6 * m_burst));

    for (Point &p : m_points) {
        // A ring point in the xz plane, with the band's half twist about the ring.
        const qreal cu = std::cos(p.u), su = std::sin(p.u);
        const qreal nx = cu / radius, nz = su / kDepth, nl = std::hypot(nx, nz);
        const qreal across = p.u / 2 + m_twist, w = kBand * p.v;
        const qreal x = radius * cu + w * std::cos(across) * nx / nl, y = w * std::sin(across);
        const qreal z = kDepth * su + w * std::cos(across) * nz / nl;
        p.home = {centreX + x, centreY + y * cosTilt - z * sinTilt};
        p.nearness = ((y * sinTilt + z * cosTilt) / (kDepth + kBand) + 1) / 2;

        // Freed points ride the flow; the pull home weakens the freer they are.
        const qreal freed = std::clamp((reach - p.threshold) / 0.45, 0.0, 1.0) * movement;
        const QPointF flow = curl(p.home.x() + p.drift.x(), p.home.y() + p.drift.y(), m_flowTime);
        const qreal pull = 1.5 + 12 * (1 - freed), push = 40 * freed;
        p.drift += (push * flow - pull * p.drift) * dt;
        // Each point strays at most an ellipse from its home, so nothing piles against a wall.
        const qreal stray = std::hypot(p.drift.x() / kDriftX, p.drift.y() / kDriftY);
        if (stray > 1) {
            p.drift /= stray;
        }
        p.drift.setX(std::clamp(p.home.x() + p.drift.x(), left - 3, right + 2) - p.home.x());
        p.at = p.home + p.drift;
        p.looseness = std::min(1.0, std::hypot(p.drift.x(), p.drift.y()) / 3) * freed;
    }
}

ScatterRibbon::ScatterRibbon(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    // A still figure draws no more frames, so it redraws itself in the new colours.
    connect(this, &ScatterRibbon::paletteChanged, this, [this] {
        if (!moving() && !m_trail.isNull()) {
            m_trail.fill(Qt::transparent);
            drawTrail(0);
            update();
        }
    });
}

void ScatterRibbon::itemChange(ItemChange change, const ItemChangeData &data)
{
    // Each showing starts from the still figure, not where the last one ended.
    if (change == ItemVisibleHasChanged && data.boolValue) {
        const bool wasMoving = moving();
        m_figure = RibbonFigure();
        m_trail = QImage();
        if (moving() != wasMoving) {
            emit movingChanged();
        }
    }
    QQuickPaintedItem::itemChange(change, data);
}

void ScatterRibbon::setLevel(qreal level)
{
    if (level == m_level) {
        return;
    }
    m_level = level;
    m_figure.hear(level, m_listening);
    emit levelChanged();
}

void ScatterRibbon::setListening(bool listening)
{
    if (listening == m_listening) {
        return;
    }
    const bool wasMoving = moving();
    m_listening = listening;
    emit listeningChanged();
    if (moving() != wasMoving) {
        emit movingChanged();
    }
}

void ScatterRibbon::advance(qreal seconds)
{
    const bool wasMoving = moving();
    const qreal dt = std::min(seconds, kLongestStep);
    m_figure.step(dt, size(), m_listening, m_movement);
    drawTrail(dt);
    update();
    if (moving() != wasMoving) {
        emit movingChanged();
    }
}

// Draws the points into the trail, which fades a little each frame, so
// moving points leave a short afterglow and still ones stay crisp.
void ScatterRibbon::drawTrail(qreal dt)
{
    const qreal ratio = window() ? window()->effectiveDevicePixelRatio() : 1;
    const QSize pixels = (size() * ratio).toSize();
    if (pixels.isEmpty()) {
        return;
    }
    if (m_trail.size() != pixels) {
        // Keeps the afterglow while the capsule widens.
        QImage trail(pixels, QImage::Format_ARGB32_Premultiplied);
        trail.setDevicePixelRatio(ratio);
        trail.fill(Qt::transparent);
        QPainter(&trail).drawImage(0, 0, m_trail);
        m_trail = trail;
    }

    // Rounds down, so a fading pixel reaches nothing rather than lingering
    // as a faint haze where points have been.
    const uint keep = uint(256 * std::exp(-dt / kAfterglow));
    for (int y = 0; y < m_trail.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(m_trail.scanLine(y));
        for (int x = 0; x < m_trail.width(); ++x) {
            const QRgb p = line[x];
            line[x] = qRgba(qRed(p) * keep >> 8, qGreen(p) * keep >> 8, qBlue(p) * keep >> 8, qAlpha(p) * keep >> 8);
        }
    }

    const qreal dim = m_figure.dim(), voice = m_figure.envelope();
    const QColor ink = mix(m_color, m_pausedColor, dim), freedInk = mix(m_freedColor, m_pausedColor, dim);
    for (const RibbonFigure::Point &p : m_figure.points()) {
        // Nearer points are bigger and stronger; all grow stronger with the voice.
        const qreal alpha = ((m_dark ? 0.3 : 0.26) + (m_dark ? 0.62 : 0.6) * p.nearness + 0.15 * voice) * (1 - 0.5 * dim);
        QColor color = mix(ink, freedInk, p.looseness);
        color.setAlphaF(std::min(1.0, alpha));
        const qreal radius = (0.42 + 0.38 * p.nearness) * (1 + 0.25 * p.looseness);
        splat(m_trail, p.at * ratio, radius * ratio, qPremultiply(color.rgba()));
    }
}

void ScatterRibbon::paint(QPainter *painter)
{
    if (m_trail.isNull()) {
        return;
    }
    // A soft glow under the crisp points, stronger with the voice.
    const qreal live = 1 - m_figure.dim();
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->setOpacity((m_dark ? 0.55 : 0.28) + (m_dark ? 0.35 : 0.12) * m_figure.envelope() * live);
    painter->drawImage(boundingRect(), m_trail.scaled(m_trail.size() / (kGlowShrink * m_trail.devicePixelRatio()),
                                                      Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    painter->setOpacity(1);
    painter->drawImage(boundingRect(), m_trail);
}

} // namespace speecher
