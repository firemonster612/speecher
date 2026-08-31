#pragma once

#include <Qt>

class QWidget;

// Native macOS chrome: the vibrancy panel that turns the sidebar column into a
// Mac settings sidebar, and the HUD panel that turns the dictation popup into a
// floating glass panel. Every entry point is a no-op when the widget has no
// native window yet, so callers never have to check.
namespace speecher::mac {

// Hides the titlebar, extends the content view underneath it so the traffic
// lights float over the sidebar, and injects the sidebar vibrancy panel
// `sidebarWidth` points wide. Safe to call twice — the panel is reused.
void applyMainWindowChrome(QWidget *window, int sidebarWidth);

// Resizes the sidebar vibrancy panel after a splitter drag or a window resize.
void updateSidebarWidth(QWidget *window, int sidebarWidth);

// Backs the whole popup with a rounded HUD-material panel and clears the Qt
// window background so the blur is what shows.
void applyPopupChrome(QWidget *popup);

// Points AppKit's own chrome at the theme the user picked. Qt::ColorScheme's
// Unknown means "follow the system", which is NSApp.appearance = nil.
void applyAppearance(Qt::ColorScheme scheme);

} // namespace speecher::mac
