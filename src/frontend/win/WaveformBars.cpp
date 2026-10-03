#include "frontend/win/WaveformBars.h"

#include <algorithm>
#include <cmath>

namespace speecher::win {
namespace {

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

// The Linux waveform's dot geometry in DIPs: 3.2 wide with 3.2 between, so
// the fifteen bars span 92.8.
constexpr double barWidth = 3.2;
constexpr double barGap = 3.2;
constexpr float barDotHeight = 3.2f;
constexpr double stripWidth = waveform::barCount * barWidth + (waveform::barCount - 1) * barGap;

} // namespace

WaveformBars::WaveformBars(QObject *parent)
    : QObject(parent)
{
    m_bars.Orientation(Orientation::Horizontal);
    m_bars.Spacing(barGap);
    m_bars.Width(stripWidth);
    m_bars.VerticalAlignment(VerticalAlignment::Center);
    m_bars.HorizontalAlignment(HorizontalAlignment::Center);
    for (int i = 0; i < waveform::barCount; ++i) {
        Shapes::Rectangle bar;
        bar.Width(barWidth);
        bar.RadiusX(0.8);
        bar.RadiusY(0.8);
        bar.Height(barDotHeight);
        bar.VerticalAlignment(VerticalAlignment::Center);
        m_bars.Children().Append(bar);
        m_rects.push_back(bar);
    }
    m_clock.start();
    m_timer.setInterval(waveform::frameIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, [this] { animate(); });
}

void WaveformBars::setLevel(float level)
{
    m_level.addChunk(level);
}

void WaveformBars::setRunning(bool running)
{
    if (running == m_timer.isActive()) {
        return;
    }
    if (running) {
        m_lastFrame = m_clock.elapsed();
        m_timer.start();
    } else {
        m_timer.stop();
    }
}

void WaveformBars::setFrozen(bool frozen)
{
    m_frozen = frozen;
    m_bars.Opacity(frozen ? 0.4 : 1.0);
}

void WaveformBars::setPaused(bool paused, const Media::Brush &pausedFill)
{
    m_pausedFill = pausedFill;
    if (paused) {
        flatten();
        fill(pausedFill ? pausedFill : m_ink);
    } else if (m_paused) {
        fill(m_ink);
        m_level.restart(m_clock.elapsed());
    }
    m_paused = paused;
}

void WaveformBars::setInk(const Media::Brush &ink)
{
    m_ink = ink;
    fill(m_paused && m_pausedFill ? m_pausedFill : ink);
}

void WaveformBars::restart()
{
    m_level.restart(m_clock.elapsed());
}

void WaveformBars::animate()
{
    const qint64 now = m_clock.elapsed();
    const float elapsed = std::clamp((now - m_lastFrame) / 1000.0f, 0.0f, 0.1f);
    m_lastFrame = now;
    if (m_frozen || m_paused) {
        return;
    }
    m_phase = std::fmod(m_phase + elapsed, 1.0f);
    m_level.advance(now);
    for (int i = 0; i < count(); ++i) {
        const float phase = m_phase - float(i) / waveform::barCount;
        const float height = float(barDotHeight * m_level.audioScale() * waveform::bulge(i)
                                   * waveform::waveMultiplier(phase - std::floor(phase)));
        m_rects[i].Height(height);
        m_rects[i].RadiusY(height / 4);
    }
}

void WaveformBars::fill(const Media::Brush &brush)
{
    if (!brush) {
        return;
    }
    for (auto &bar : m_rects) {
        bar.Fill(brush);
    }
}

void WaveformBars::flatten()
{
    for (auto &bar : m_rects) {
        bar.Height(barDotHeight);
        bar.RadiusY(0.8);
    }
}

} // namespace speecher::win
