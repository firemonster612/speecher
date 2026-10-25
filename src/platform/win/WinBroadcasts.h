#pragma once

#include <functional>

class QObject;

namespace speecher::win {

// Windows tells top-level windows about some things and nothing else, and
// Speecher may have none on screen, so each of these keeps a hidden one
// until context is destroyed. The callback runs on the thread that called,
// from the window's message handler.

// Calls quit when Windows asks Speecher to close: at logoff and shutdown, and
// when Restart Manager closes it so Setup can replace its files.
void quitWhenWindowsEndsSession(QObject *context, std::function<void()> quit);

// Calls changed when the person picks another theme in Windows' settings,
// for the taskbar or for apps. Qt reports only the apps' theme.
void onThemeChanged(QObject *context, std::function<void()> changed);

} // namespace speecher::win
