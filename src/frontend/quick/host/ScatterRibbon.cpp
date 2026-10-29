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
// Each loop's span (px): the figure's room in the 280 capsule's strip, so
// it keeps that size in the 460 capsule's.
constexpr qreal kSpan = 148;
// How much wider the strip is in the 460 capsule (px), and how far the two
// loops overlap where they link.
constexpr qreal kWiden = 460 - 280;
constexpr qreal kLink = 5;
// Ring depth (px) before the tilt.
constexpr qreal kDepth = 12;
// A level under this is the microphone's own noise.
constexpr qreal kSilent = 0.02;
// How long a moving point's afterglow lasts (s).
constexpr qreal kAfterglow = 0.06;
// The glow is the trail shrunk this many px to one and stretched back,
// which blurs it by about 1.6 px.
constexpr qreal kGlowShrink = 3;
// A stalled frame resumes the figure rather than leaping it on.
constexpr qreal kLongestStep = 0.05;
// Frames at 60 fps for a still figure's afterglow to build up fully.
constexpr int kSettleFrames = 30;

// Eases value toward target, most of the way in about ms, at any frame
// rate; at 0 ms it jumps there.
qreal follow(qreal value, qreal target, qreal ms, qreal dt)
{
    return value + (target - value) * (1 - std::exp(-dt / std::max(1e-4, ms / 3000)));
}

// What each loudness (0 to 1) looks like: the loop's tilt (rad), which rests
// open, its band's half-width (px) and the share of points freed.
qreal tiltOf(qreal loudness) { return 0.66 + 0.32 * std::pow(loudness, 0.8); }
qreal bandOf(qreal loudness) { return 1.1 + 4.2 * loudness; }
qreal shareOf(qreal loudness) { return 0.7 * std::pow(loudness, 1.5); }

// An angle wrapped to -pi..pi.
qreal wrapAngle(qreal angle)
{
    return angle - 2 * std::numbers::pi * std::round(angle / (2 * std::numbers::pi));
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
    m_points.reserve(2 * kStrands * kPerStrand);
    for (const bool second : {false, true}) {
        for (int strand = 0; strand < kStrands; ++strand) {
            for (int i = 0; i < kPerStrand; ++i) {
                Point point;
                point.u = (i + qreal(strand) / kStrands) / kPerStrand * 2 * std::numbers::pi;
                point.v = qreal(strand) / (kStrands - 1) * 2 - 1;
                point.threshold = random.generateDouble();
                point.second = second;
                m_points.append(point);
            }
        }
    }
}

qreal RibbonFigure::rest() const
{
    return std::pow(1 - std::min(1.0, m_loudness / 0.4), 2);
}

void RibbonFigure::hear(qreal level, bool listening)
{
    m_level = std::clamp(level, 0.0, 1.0);
    const qreal shown = m_level < kSilent ? 0 : std::sqrt(m_level);
    // A syllable lands: the level jumps well above where it has been.
    const qreal onset = std::max(0.0, shown - m_smoothed);
    if (!listening || onset < 0.06) {
        return;
    }
    m_burst = std::min(1.0, m_burst + onset * 2.5);
    if (m_time - m_lastPulse > 0.1) {
        m_pulses.append({m_time, std::min(1.0, 0.35 + onset * 2.5)});
        m_lastPulse = m_time;
    }
}

void RibbonFigure::step(qreal dt, QSizeF strip, bool listening, qreal movement)
{
    const qreal left = kInset, right = strip.width() - kInset, centreY = strip.height() / 2;
    // A narrower strip, as the capsule shrinks away, squeezes the loop to fit.
    const qreal span = std::min(kSpan, right - left), radius = span / 2 - 3;
    // Too narrow to hold the loop, as before the popup lays it out.
    if (radius <= 0) {
        return;
    }

    m_time += dt;
    // Where the level has been, eased frame by frame, so a syllable after
    // a quiet stretch counts from the quiet even when no new level arrived.
    const qreal shown = m_level < kSilent ? 0 : std::sqrt(m_level);
    m_smoothed = follow(m_smoothed, shown, shown > m_smoothed ? 120 : 480, dt);
    const qreal voice = listening && m_level >= kSilent ? std::min(1.0, std::pow(m_level, 0.6) / 0.85) : 0;
    m_loudness = follow(m_loudness, voice, voice > m_loudness ? 150 : 700, dt);
    m_dim = follow(m_dim, listening ? 0 : 1, 280, dt);
    m_burst *= std::exp(-dt / 0.25);
    m_pulses.removeIf([this](const Pulse &pulse) { return m_time - pulse.born >= 1.2; });
    // The twist crawls round the loop and the loop rocks, both slowly at
    // rest and faster with the voice; paused, it holds still.
    const qreal loud = m_loudness, rest = this->rest(), run = (1 - m_dim) * movement;
    m_twist += dt * (0.25 + 1.6 * loud) * run;
    m_rock += dt * (0.2 + 0.5 * loud) * run;
    m_flowTime += dt * (0.3 + 1.5 * loud);
    // At rest the loop holds its silent tilt and a wider band; reduced
    // motion holds it half open with a mid band.
    const qreal tilt = std::lerp(0.6, std::lerp(tiltOf(loud), tiltOf(0), rest), movement) + 0.07 * std::sin(m_rock);
    const qreal cosTilt = std::cos(tilt), sinTilt = std::sin(tilt);
    const qreal band = std::lerp(2.4, bandOf(loud) + 1.2 * rest, movement);
    const qreal share = std::min(0.95, (shareOf(loud) + 0.1 * m_burst * (0.3 + loud)) * movement);
    const qreal driftX = 5 + 13 * loud, driftY = 2 + 4 * loud;

    // Where the loops sit. The unfurl runs 0 to 1 as the strip widens from
    // the 280 capsule's to the 460's; it follows more calmly than the
    // capsule's spring, and never ahead of the room the strip has. The pair
    // needs extra more than one loop and stays centred; the second loop's
    // arms unfurl from the link only as far as the unfurl allows.
    const qreal room = std::clamp((right - left - span) / kWiden, 0.0, 1.0);
    m_unfurl = m_unfurl < 0 ? room : std::min(room, follow(m_unfurl, room, 600 * movement, dt));
    const qreal extra = 2 * radius - kLink;
    const qreal centre = left + m_unfurl * (kWiden - extra) / 2 + 3 + radius, secondCentre = centre + extra;
    const qreal reach = std::acos(1 - std::min(2.0, (kLink + m_unfurl * extra) / radius));
    const qreal secondShown = std::clamp(m_unfurl * extra / 10, 0.0, 1.0);

    for (Point &p : m_points) {
        if (p.second) {
            p.shown = secondShown * std::clamp((reach - std::abs(wrapAngle(p.u))) / 0.3, 0.0, 1.0);
            if (p.shown <= 0) {
                p.drift = {};
                continue;
            }
        }
        // A ring point in the xz plane, with the band's half twist about the
        // ring. The second loop mirrors the first and turns the other way.
        const qreal cu = std::cos(p.u), su = std::sin(p.u);
        const qreal nx = cu / radius, nz = su / kDepth, nl = std::hypot(nx, nz);
        const qreal across = p.u / 2 + (p.second ? -m_twist : m_twist), w = band * p.v;
        const qreal x = radius * cu + w * std::cos(across) * nx / nl, y = w * std::sin(across);
        const qreal z = kDepth * su + w * std::cos(across) * nz / nl;
        p.home = {p.second ? secondCentre - x : centre + x, centreY + y * cosTilt - z * sinTilt};
        p.nearness = ((y * sinTilt + z * cosTilt) / (kDepth + band) + 1) / 2;

        // Syllable pulses start at the front of the loop and run both ways round it.
        p.glow = 0;
        for (const Pulse &pulse : m_pulses) {
            const qreal age = m_time - pulse.born;
            const qreal off = std::abs(std::abs(wrapAngle(p.u - std::numbers::pi / 2)) - age * 7);
            p.glow = std::max(p.glow, pulse.strength * std::exp(-age / 0.35 - off * off / 0.25));
        }
        p.glow *= movement;

        // Freed points ride the flow; the pull home weakens the freer they are.
        const qreal freed = std::clamp((share - p.threshold) / 0.06 + 0.5, 0.0, 1.0) * movement;
        const QPointF flow = curl(p.home.x() + p.drift.x(), p.home.y() + p.drift.y(), m_flowTime);
        const qreal pull = 1.5 + 12 * (1 - freed), push = 40 * freed;
        p.drift += (push * flow - pull * p.drift) * dt;
        // Each point strays at most an ellipse from its home, which grows with
        // loudness, so nothing piles against a wall.
        const qreal stray = std::hypot(p.drift.x() / driftX, p.drift.y() / driftY);
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
    // A still figure draws no more frames, so it redraws itself in the new
    // colours: over enough frames for the afterglow to build up as it does
    // when the figure settles.
    connect(this, &ScatterRibbon::paletteChanged, this, [this] {
        if (!moving() && !m_trail.isNull()) {
            m_trail.fill(Qt::transparent);
            for (int frame = 0; frame < kSettleFrames; ++frame) {
                drawTrail(1.0 / 60);
            }
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
        // Keeps hearing the level it has, which may not change again soon.
        m_figure.hear(m_level, false);
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

    // Ink in silence, warming toward the accent with loudness; freed points
    // and pulses go all the way.
    const qreal dim = m_figure.dim(), loud = m_figure.loudness(), rest = m_figure.rest();
    const QColor ink = mix(mix(m_color, m_freedColor, 0.7 * loud), m_pausedColor, dim);
    const QColor freedInk = mix(m_freedColor, m_pausedColor, dim);
    for (const RibbonFigure::Point &p : m_figure.points()) {
        // At rest only the band's two edge strands show, a little finer.
        const bool edge = std::abs(p.v) == 1;
        // Nearer points are bigger and stronger; all grow stronger with the voice.
        const qreal alpha = ((m_dark ? 0.3 : 0.26) + (m_dark ? 0.62 : 0.6) * p.nearness + 0.15 * loud + 0.35 * p.glow)
                            * (1 - 0.5 * dim) * p.shown * std::lerp(1.0, edge ? 0.95 : 0, rest);
        if (alpha <= 0.004) {
            continue;
        }
        QColor color = mix(ink, freedInk, std::max(p.looseness * (1 - rest), p.glow));
        color.setAlphaF(std::min(1.0, alpha));
        const qreal radius = (0.42 + 0.38 * p.nearness) * (1 + 0.25 * p.looseness + 0.35 * p.glow) * std::lerp(1.0, edge ? 0.95 : 1, rest);
        splat(m_trail, p.at * ratio, radius * ratio, qPremultiply(color.rgba()));
    }
}

void ScatterRibbon::paint(QPainter *painter)
{
    if (m_trail.isNull()) {
        return;
    }
    // At its own size, so a trail not yet redrawn for a new width is never stretched.
    const QRectF area(QPointF(), m_trail.deviceIndependentSize());
    // A soft glow under the crisp points, stronger with the voice and gone at rest.
    const qreal live = 1 - m_figure.dim(), glow = 1 - m_figure.rest();
    if (glow > 0) {
        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        painter->setOpacity(((m_dark ? 0.55 : 0.28) + (m_dark ? 0.35 : 0.12) * m_figure.loudness() * live) * glow);
        painter->drawImage(area, m_trail.scaled(m_trail.size() / (kGlowShrink * m_trail.devicePixelRatio()),
                                                Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        painter->setOpacity(1);
    }
    painter->drawImage(area, m_trail);
}

} // namespace speecher
