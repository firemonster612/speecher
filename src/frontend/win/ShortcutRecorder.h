#pragma once

#include "frontend/win/SettingsPage.h"

namespace speecher::win {

// The Global Shortcut row at the top of Dictation: the current binding and a
// "Set shortcut…" button that records the next one in a modal dialog — a key
// combination, a bare key, or a lone modifier committed on its release — with
// a reset to the default. The row says afterwards when the binder refused a
// binding or when a single key also types.
class ShortcutRecorder {
public:
    // The schema's "globalShortcut" custom row, which its card holds; the
    // row's label titles the dialog.
    static winrt::Microsoft::UI::Xaml::Controls::StackPanel element(const RowSnapshot &row,
                                                                    PaneHost &host);

    // Starts or ends recording, suspending the hotkey registration while it
    // runs: the bound chord is consumed system-wide and would never reach the
    // recorder (it would start a dictation instead). Balanced against the
    // binder's suspension count and idempotent; the dialog's Closed ends it,
    // and the window's teardown does too in case the dialog never closed.
    static void setRecording(PaneHost &host, bool recording);

    // The Qt key a Windows virtual key stands for on the active keyboard
    // layout, or 0 for modifiers and keys no layout can print. Shared with the
    // setup assistant's recorder so both accept the same keys.
    static int qtKeyForVirtualKey(int virtualKey);
    static Qt::KeyboardModifiers heldModifiers();

    // Whether a Windows virtual key is a modifier: part of a combination when
    // another key joins it, a single-key binding when pressed and released on
    // its own. Shared with the setup assistant's recorder.
    static bool isModifierKey(int virtualKey);
};

} // namespace speecher::win
