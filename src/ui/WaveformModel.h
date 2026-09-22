#pragma once

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <iterator>

// The motion model of the waveform, a port of Wispr Flow's status-bar bars
// (v1.6.793), shared by the Qt waveform widget and the Windows dictation
// panel so a tuning change cannot land on one platform and miss the other
// (the macOS panel is a Swift port and stays one). Each bar is a rounded dot
// scaled vertically about its centre by
//
//   audioScale * bulge * wave
//
// where audioScale is the smoothed mic level times a gain of 5 floored at 1,
// bulge weights bars towards the centre, and wave is a 1s keyframe loop
// (1 -> 1.2 -> 1.5 -> 1.1 -> 1.3 -> 1, ease-in-out between keyframes) whose
// phase trails one bar's share of the loop per bar, so a crest travels across
// the row once per second and wraps seamlessly.
namespace speecher::waveform {

// Wispr Flow's row is ten bars in a 50px pill. Speecher's pill is wider, so
// it holds proportionally more of the same bars rather than stretching them:
// fifteen at Wispr Flow's thickness and spacing fill 74% of the width, the
// same fraction its ten fill of 50px.
inline constexpr int barCount = 15;
inline constexpr float audioGain = 5.0f;
inline constexpr float levelSpanDb = 20.0f;
// Wispr Flow stops the floor descending past -60 dBFS of the raw capture, so
// one freakishly quiet chunk cannot leave the display permanently
// oversensitive. Speecher's level signal is pre-gained and its gain differs
// per audio input (the microphone emits rms*8 clipped at 1, the file input a
// unity peak), so there is no single dBFS equivalent; -46dB is below room tone
// on both paths, which is what the clamp is there to protect.
inline constexpr float dbFloorLimit = -46.0f;
inline constexpr int levelAverageMs = 150;
// The tick rate the 0.85 smoothing factor assumes: Wispr Flow smooths once per
// display frame in a requestAnimationFrame loop.
inline constexpr int frameIntervalMs = 16;
// Wispr Flow's bulge falls off with the square of a bar's distance from the
// centre for a short row, and linearly once its bulgeCoefficient reaches 2,
// which is the branch a row this long wants: the quadratic would flatten the
// outermost bars to nothing.
inline constexpr qreal bulgeCoefficient = 2.0;

struct WaveKeyframe {
    qreal at;
    qreal value;
};
inline constexpr WaveKeyframe waveKeyframes[] = {
    {0.0, 1.0}, {0.2, 1.2}, {0.4, 1.5}, {0.8, 1.1}, {0.9, 1.3}, {1.0, 1.0}};

// CSS ease-in-out, cubic-bezier(0.42, 0, 0.58, 1): solve x(t) = s for t by
// Newton's method, then return y(t). With y control points 0 and 1, y(t)
// reduces to t^2 * (3 - 2t).
inline qreal easeInOut(qreal s)
{
    constexpr qreal p1x = 0.42;
    constexpr qreal p2x = 0.58;
    qreal t = s;
    for (int i = 0; i < 6; ++i) {
        const qreal oneMinusT = 1.0 - t;
        const qreal x = 3.0 * p1x * t * oneMinusT * oneMinusT
            + 3.0 * p2x * t * t * oneMinusT + t * t * t;
        const qreal dx = 3.0 * p1x * (1.0 - 4.0 * t + 3.0 * t * t)
            + 3.0 * p2x * (2.0 * t - 3.0 * t * t) + 3.0 * t * t;
        if (dx <= 0.0) {
            break;
        }
        t = std::clamp(t - (x - s) / dx, 0.0, 1.0);
    }
    return t * t * (3.0 - 2.0 * t);
}

inline qreal waveMultiplier(qreal phase)
{
    constexpr int segments = int(std::size(waveKeyframes)) - 1;
    for (int i = 0; i < segments; ++i) {
        const WaveKeyframe &from = waveKeyframes[i];
        const WaveKeyframe &to = waveKeyframes[i + 1];
        if (phase > to.at) {
            continue;
        }
        const qreal progress = (phase - from.at) / (to.at - from.at);
        return from.value + (to.value - from.value) * easeInOut(progress);
    }
    return waveKeyframes[segments].value;
}

inline qreal bulge(int barIndex)
{
    const qreal distance = std::abs((barCount - 1) / 2.0 - barIndex);
    return std::max(0.0, 1.0 - distance * (bulgeCoefficient / 48.0));
}

// The audio half of the waveform, kept apart from the painting so the mapping
// can be tested without a widget: every capture chunk is mapped through an
// adaptive noise floor, the chunks are averaged over 150ms windows, and each
// window's mean is smoothed once per frame.
class LevelModel {
public:
    // One capture chunk. A level of zero is silence, which pulls the bars
    // back down but does not train the noise floor.
    void addChunk(float level)
    {
        // A silent chunk still counts towards the window mean, so a muted
        // microphone or the end of a session brings the bars back to rest;
        // only the noise floor ignores it, because log10(0) has no floor to
        // learn.
        float mapped = 0.0f;
        if (level > 0.0f) {
            const float db = 20.0f * std::log10(level);
            if (db < m_dbFloor) {
                m_dbFloor = std::max(dbFloorLimit, db);
            }
            mapped = std::clamp((db - m_dbFloor) / levelSpanDb, 0.0f, 1.0f);
        }
        m_windowSum += mapped;
        ++m_windowCount;
    }

    // One display frame.
    void advance(qint64 nowMs)
    {
        if (nowMs - m_windowStartMs >= levelAverageMs) {
            if (m_windowCount > 0) {
                m_target = m_windowSum / float(m_windowCount);
                m_windowSum = 0.0f;
                m_windowCount = 0;
            }
            // Advance on the 150ms grid: assigning nowMs here would stretch
            // every window to the next frame boundary, averaging 160ms of
            // audio.
            m_windowStartMs += levelAverageMs;
            if (nowMs - m_windowStartMs >= levelAverageMs) {
                m_windowStartMs = nowMs;
            }
        }
        // Per-frame exponential smoothing, quantised to 0.01 steps.
        m_smoothed = std::floor((m_smoothed * 0.85f + m_target * 0.15f) * 100.0f) / 100.0f;
    }

    // Starts a fresh capture. The noise floor survives, as it does in
    // Wispr Flow, so the room stays calibrated across sessions.
    void restart(qint64 nowMs)
    {
        m_windowSum = 0.0f;
        m_windowCount = 0;
        m_windowStartMs = nowMs;
        m_target = 0.0f;
        m_smoothed = 0.0f;
    }

    float audioScale() const { return std::max(1.0f, audioGain * m_smoothed); }

private:
    float m_dbFloor = 0.0f;
    float m_windowSum = 0.0f;
    int m_windowCount = 0;
    qint64 m_windowStartMs = 0;
    float m_target = 0.0f;
    float m_smoothed = 0.0f;
};

} // namespace speecher::waveform
