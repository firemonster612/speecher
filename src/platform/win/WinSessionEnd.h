#pragma once

#include <functional>

class QObject;

namespace speecher::win {

// Calls quit when Windows asks Speecher to close: at logoff and shutdown, and
// when Restart Manager closes it so Setup can replace its files. Those
// requests go to top-level windows, and Speecher may have none on screen, so
// this keeps a hidden one until context is destroyed. quit runs on the
// thread that called this, from the window's message handler.
void quitWhenWindowsEndsSession(QObject *context, std::function<void()> quit);

} // namespace speecher::win
