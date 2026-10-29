#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QQmlEngine>
#include <QQuickPaintedItem>
#include <QSizeF>
#include <QVector>

namespace speecher {

// The scatter ribbon's motion, apart from its drawing: 520 points a loop, on
// five strands, form a turning one-twist band (a Möbius loop seen at a tilt). The
// loop keeps the size it has in the 280 capsule's strip; when the strip is
// wide enough, a second, mirrored loop unfurls from the link beside it and
// turns the other way, so the pair reads as ∞. A held loudness opens the
// loop's tilt, widens its band, speeds its turn and frees a growing share of
// points, which a divergence-free curl flow carries off in streams, each
// within an ellipse of its home; each syllable sends a pulse round the loop.
class RibbonFigure {
public:
    struct Point {
        // Where it is drawn, in the strip's coordinates.
        QPointF at;
        // 0 at the back of the loop to 1 at the front.
        qreal nearness = 0;
        // 0 on the figure to 1 when the voice has freed it and it has strayed.
        qreal looseness = 0;
        // How much of it shows, 0 to 1: the second loop's points show as it unfurls.
        qreal shown = 1;
        // A syllable's pulse passing it, 0 to 1.
        qreal glow = 0;
        // Its place round the loop (radians) and across the band (-1 to 1),
        // and how loud the voice must be to free it.
        qreal u = 0;
        qreal v = 0;
        qreal threshold = 0;
        // On the second loop, which mirrors the first.
        bool second = false;
        // Its home on the figure and how far it has strayed from it.
        QPointF home;
        QPointF drift;
    };

    RibbonFigure();

    // A new voice level, 0 to 1, as it arrives (about every 50 ms). A syllable
    // landing while listening frees a short burst of extra points and sends
    // a pulse round the loop.
    void hear(qreal level, bool listening);
    // Moves the figure on by dt seconds along a strip of size strip. Not
    // listening, it stops turning and draws its points back home. movement
    // is Theme.movement: at 0 nothing turns, scatters or pulses, and the
    // second loop follows the strip's width at once.
    void step(qreal dt, QSizeF strip, bool listening, qreal movement);

    const QVector<Point> &points() const { return m_points; }
    // How loud the voice is, 0 to 1: a quick rise and a slow fall, so it
    // holds between syllables.
    qreal loudness() const { return m_loudness; }
    // How much the resting look shows, 1 in silence to 0 by 40% loudness:
    // only the band's edge strands, finer, with no glow.
    qreal rest() const;
    // 0 while listening, easing to 1 once paused.
    qreal dim() const { return m_dim; }
    // Paused and done changing: dimmed, the voice let go and the last
    // pulse gone, so further steps draw the same figure.
    bool settled() const { return m_dim > 0.995 && m_loudness < 0.002 && m_pulses.isEmpty(); }

private:
    // A syllable's pulse: when it set off (on the figure's clock) and how bright it is.
    struct Pulse {
        qreal born = 0;
        qreal strength = 0;
    };

    QVector<Point> m_points;
    QVector<Pulse> m_pulses;
    qreal m_time = 0;
    qreal m_lastPulse = -1;
    qreal m_level = 0;
    qreal m_smoothed = 0;
    qreal m_loudness = 0;
    qreal m_dim = 0;
    qreal m_burst = 0;
    qreal m_twist = 0;
    qreal m_rock = 0;
    qreal m_flowTime = 0;
    // How far the second loop has unfurled, 0 to 1; below 0 until the first
    // step, which starts it as far as the strip allows.
    qreal m_unfurl = -1;
};

// The dictation popup's level animation while listening and paused: a
// RibbonFigure drawn as small dots, nearer ones bigger and stronger, warming
// toward freedColor with the voice and fully for freed points and pulses,
// over a short afterglow and a soft glow. In silence only the band's edge
// strands show, with no glow. Paused, it dims toward pausedColor. Call
// advance once a frame while moving.
class ScatterRibbon : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    // The voice level, 0 to 1.
    Q_PROPERTY(qreal level READ level WRITE setLevel NOTIFY levelChanged)
    // False while paused.
    Q_PROPERTY(bool listening READ listening WRITE setListening NOTIFY listeningChanged)
    // Theme.movement: at 0 only the brightness and colour follow the voice.
    Q_PROPERTY(qreal movement MEMBER m_movement)
    // Theme.dark: the dots and glow are a little stronger on a dark sheet.
    Q_PROPERTY(bool dark MEMBER m_dark NOTIFY paletteChanged)
    Q_PROPERTY(QColor color MEMBER m_color NOTIFY paletteChanged)
    Q_PROPERTY(QColor freedColor MEMBER m_freedColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor pausedColor MEMBER m_pausedColor NOTIFY paletteChanged)
    // Whether advance still changes anything: while listening, and while it
    // settles once paused.
    Q_PROPERTY(bool moving READ moving NOTIFY movingChanged)

public:
    explicit ScatterRibbon(QQuickItem *parent = nullptr);

    qreal level() const { return m_level; }
    void setLevel(qreal level);
    bool listening() const { return m_listening; }
    void setListening(bool listening);
    bool moving() const { return m_listening || !m_figure.settled(); }

    // Moves the figure on by seconds and draws the next frame.
    Q_INVOKABLE void advance(qreal seconds);
    void paint(QPainter *painter) override;

protected:
    void itemChange(ItemChange change, const ItemChangeData &data) override;

signals:
    void levelChanged();
    void listeningChanged();
    void movingChanged();
    void paletteChanged();

private:
    void drawTrail(qreal dt);

    RibbonFigure m_figure;
    // The points of recent frames, fading a little each frame.
    QImage m_trail;
    qreal m_level = 0;
    bool m_listening = false;
    qreal m_movement = 1;
    bool m_dark = false;
    QColor m_color;
    QColor m_freedColor;
    QColor m_pausedColor;
};

} // namespace speecher
