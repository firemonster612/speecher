#pragma once

#include <windows.h>

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime
#include <winrt/Microsoft.UI.Xaml.h>
#pragma pop_macro("GetCurrentTime")

namespace speecher::win {

struct PaneHost;

// Home: the dictation card, then the insights InsightsSummary computes for the
// period on the host, in the settings page's scaffold and cards.
winrt::Microsoft::UI::Xaml::UIElement buildHomePage(PaneHost &host);

} // namespace speecher::win
