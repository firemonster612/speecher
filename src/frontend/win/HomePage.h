#pragma once

#include <windows.h>

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime
#include <winrt/Microsoft.UI.Xaml.h>
#pragma pop_macro("GetCurrentTime")

#include <functional>

namespace speecher::win {

struct PaneHost;

// Home: the dictation card, then the insights InsightsSummary computes for the
// period on the host, in the settings page's scaffold and cards.
winrt::Microsoft::UI::Xaml::UIElement buildHomePage(PaneHost &host);

// What Home's "Copy image with stats" does: the period and measure on the
// host as a picture on the clipboard. done says whether it got there.
winrt::fire_and_forget copyStatsImage(PaneHost &host, std::function<void(bool copied)> done);

} // namespace speecher::win
