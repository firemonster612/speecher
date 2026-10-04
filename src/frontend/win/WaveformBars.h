#pragma once

#include "ui/WaveformModel.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#pragma pop_macro("GetCurrentTime")

#include <vector>

namespace speecher::win {

// A row of dots in DIPs. The defaults are Home's fifteen, at the Linux
// waveform's 3.2 wide with 3.2 between; the popup passes its own.
struct WaveformGeometry {
    int count = waveform::barCount;
    double barWidth = 3.2;
    double barGap = 3.2;
    double dotHeight = 3.2;

    double stripWidth() const { return count * barWidth + (count - 1) * barGap; }
};

// The dictation waveform's dots, as the panel and Home draw them: the level
// model and travelling crest shared with Linux and macOS (ui/WaveformModel.h),
// as rectangles in a horizontal strip. Paused, the row lies flat and still in
// the caution colour.
class WaveformBars final : public QObject {
public:
    explicit WaveformBars(WaveformGeometry geometry = {}, QObject *parent = nullptr);

    // The dots' width in DIPs.
    double stripWidth() const { return m_geometry.stripWidth(); }

    winrt::Microsoft::UI::Xaml::Controls::StackPanel element() const { return m_bars; }
    int count() const { return int(m_rects.size()); }
    void setLevel(float level);
    // Animates while running; a hidden strip stops.
    void setRunning(bool running);
    // Holds the bars where they are, dimmed, as the session freezes the popup.
    void setFrozen(bool frozen);
    // A flat, still row in fill while paused; the ink otherwise.
    void setPaused(bool paused, const winrt::Microsoft::UI::Xaml::Media::Brush &fill);
    void setInk(const winrt::Microsoft::UI::Xaml::Media::Brush &ink);
    // Starts the level capture afresh, as a new dictation does.
    void restart();

private:
    void animate();
    void fill(const winrt::Microsoft::UI::Xaml::Media::Brush &brush);
    void flatten();

    WaveformGeometry m_geometry;
    winrt::Microsoft::UI::Xaml::Controls::StackPanel m_bars;
    std::vector<winrt::Microsoft::UI::Xaml::Shapes::Rectangle> m_rects;
    winrt::Microsoft::UI::Xaml::Media::Brush m_ink{nullptr};
    winrt::Microsoft::UI::Xaml::Media::Brush m_pausedFill{nullptr};
    QTimer m_timer;
    QElapsedTimer m_clock;
    qint64 m_lastFrame = 0;
    waveform::LevelModel m_level;
    float m_phase = 0.0f;
    bool m_frozen = false;
    bool m_paused = false;
};

} // namespace speecher::win
