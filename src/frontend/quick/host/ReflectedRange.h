#pragma once

#include <QColor>
#include <QQmlEngine>
#include <QQuickItem>

#include <array>

namespace speecher {

// The voice as the reflected range draws it: auto level against the
// speaker's recent peak and the room's floor, square-root loudness and a
// spring on the amplitude, sampled 60 times a second into a short history,
// newest first. Every ridge is this history read at a different age.
class RangeVoice {
public:
    static constexpr int kSamples = 96;
    static constexpr qreal kRate = 60;

    // A new voice level, 0 to 1, as it arrives (about every 50 ms).
    void hear(qreal level) { m_level = level; }
    // Moves on by dt seconds. Not listening, the voice goes quiet, the
    // history drains and the clock slows to a stop. calm is reduced motion:
    // one slowly smoothed level fills the whole history and the clock holds.
    void step(qreal dt, bool listening, bool calm);

    const std::array<float, kSamples> &history() const { return m_history; }
    // Samples since the newest one, 0 to 1.
    qreal sub() const { return m_sub; }
    // The voice's clock in samples, continuous between them.
    qreal clock() const { return m_count + m_sub; }
    // Seconds of carrier motion, which stops while paused.
    qreal time() const { return m_time; }
    // 0 while listening, easing to 1 once paused.
    qreal dim() const { return m_dim; }
    // Paused and done changing, so further steps draw the same frame.
    bool settled() const;

private:
    void tick(qreal loudness, bool calm);

    std::array<float, kSamples> m_history{};
    qreal m_level = 0;
    qreal m_sub = 0;
    qint64 m_count = 0;
    qreal m_time = 0;
    qreal m_pace = 1;
    qreal m_dim = 0;
    qreal m_amp = 0;
    qreal m_velocity = 0;
    qreal m_peak = 0.2;
    qreal m_floor = 0;
};

// The dictation popup's voice while listening and paused: the ridge range
// folded about its centre line. Now is a bright line through the middle;
// the voice opens it into a lens, and older moments stack outward above and
// below it, each ridge the voice a little earlier, fading and hidden where a
// newer one passes it. One fragment shader draws it from RangeVoice's
// history; the software renderer, which runs no shaders, gets the same
// frame drawn on the CPU. Paused, it closes, stills and dims toward
// pausedColor. Call advance once a frame while moving.
class ReflectedRange : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    // The voice level, 0 to 1.
    Q_PROPERTY(qreal level READ level WRITE setLevel NOTIFY levelChanged)
    // False while paused.
    Q_PROPERTY(bool listening READ listening WRITE setListening NOTIFY listeningChanged)
    // Theme.movement: at 0 the whole stack rises and falls as one, slowly.
    Q_PROPERTY(qreal movement MEMBER m_movement)
    // Theme.dark: the range is light on a dark sheet, ink on a light one.
    Q_PROPERTY(bool dark MEMBER m_dark NOTIFY paletteChanged)
    // The front line, the oldest ridges and the glow around every line.
    Q_PROPERTY(QColor color MEMBER m_color NOTIFY paletteChanged)
    Q_PROPERTY(QColor farColor MEMBER m_farColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor glowColor MEMBER m_glowColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor pausedColor MEMBER m_pausedColor NOTIFY paletteChanged)
    // Whether advance still changes anything: while listening, and while it
    // settles once paused.
    Q_PROPERTY(bool moving READ moving NOTIFY movingChanged)

public:
    explicit ReflectedRange(QQuickItem *parent = nullptr);

    qreal level() const { return m_level; }
    void setLevel(qreal level);
    bool listening() const { return m_listening; }
    void setListening(bool listening);
    bool moving() const { return m_listening || !m_voice.settled(); }
    const RangeVoice &voice() const { return m_voice; }

    // Moves the voice on by seconds and draws the next frame.
    Q_INVOKABLE void advance(qreal seconds);

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
    void itemChange(ItemChange change, const ItemChangeData &data) override;

signals:
    void levelChanged();
    void listeningChanged();
    void movingChanged();
    void paletteChanged();

private:
    RangeVoice m_voice;
    qreal m_level = 0;
    bool m_listening = false;
    qreal m_movement = 1;
    bool m_dark = false;
    QColor m_color;
    QColor m_farColor;
    QColor m_glowColor;
    QColor m_pausedColor;
};

} // namespace speecher
