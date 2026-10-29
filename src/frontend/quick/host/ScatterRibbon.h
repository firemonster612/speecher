#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QQmlEngine>
#include <QQuickPaintedItem>
#include <QSizeF>
#include <QVector>

namespace speecher {

// The scatter ribbon's motion, apart from its drawing: about 520 points on
// five strands form a slowly turning one-twist band (a Möbius loop seen at a
// tilt) along a strip. The voice speeds the twist and frees points, which a
// divergence-free curl flow carries off in streams, each within an ellipse
// of its home; silence draws them back into the exact figure.
class RibbonFigure {
public:
    struct Point {
        // Where it is drawn, in the strip's coordinates.
        QPointF at;
        // 0 at the back of the loop to 1 at the front.
        qreal nearness = 0;
        // 0 on the figure to 1 when the voice has freed it and it has strayed.
        qreal looseness = 0;
        // Its place round the loop (radians) and across the band (-1 to 1),
        // and how loud the voice must be to free it.
        qreal u = 0;
        qreal v = 0;
        qreal threshold = 0;
        // Its home on the figure and how far it has strayed from it.
        QPointF home;
        QPointF drift;
    };

    RibbonFigure();

    // A new voice level, 0 to 1, as it arrives (about every 50 ms). A word
    // start while listening frees a short burst of extra points.
    void hear(qreal level, bool listening);
    // Moves the figure on by dt seconds along a strip of size strip. Not
    // listening, it stops turning and draws its points back home. movement
    // is Theme.movement: at 0 nothing turns or scatters.
    void step(qreal dt, QSizeF strip, bool listening, qreal movement);

    const QVector<Point> &points() const { return m_points; }
    // The voice's envelope, 0 to 1: fast attack, quick release.
    qreal envelope() const { return m_envelope; }
    // 0 while listening, easing to 1 once paused.
    qreal dim() const { return m_dim; }

private:
    QVector<Point> m_points;
    qreal m_level = 0;
    qreal m_smoothed = 0;
    qreal m_envelope = 0;
    qreal m_dim = 0;
    qreal m_burst = 0;
    qreal m_twist = 0;
    qreal m_rock = 0;
    qreal m_flowTime = 0;
};

// The dictation popup's level animation while listening and paused: a
// RibbonFigure drawn as small dots, nearer ones bigger and stronger, freed
// ones turning toward freedColor, over a short afterglow and a soft glow.
// Paused, it dims toward pausedColor. Call advance once a frame while moving.
class ScatterRibbon : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    // The voice level, 0 to 1.
    Q_PROPERTY(qreal level READ level WRITE setLevel NOTIFY levelChanged)
    // False while paused.
    Q_PROPERTY(bool listening READ listening WRITE setListening NOTIFY listeningChanged)
    // Theme.movement: at 0 only the brightness follows the voice.
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
    bool moving() const { return m_listening || m_figure.dim() < kSettled; }

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
    // How dim the paused figure gets before it counts as still.
    static constexpr qreal kSettled = 0.995;

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
