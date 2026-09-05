#pragma once

#include "frontend/win/SettingsPage.h"

namespace speecher::win {

// The Shortcut pane: a recorder that captures the next chord, refuses chords
// without a modifier, shows the current binding as the system writes it, a
// reset to the default, and the binder's own error when it refuses a binding.
class ShortcutRecorder {
public:
    // Appends the pane's cards to an already-titled settings column.
    static void appendPane(const winrt::Microsoft::UI::Xaml::Controls::StackPanel &column,
                           PaneHost &host);
};

} // namespace speecher::win
