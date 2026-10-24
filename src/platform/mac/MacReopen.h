#pragma once

#include <functional>

class QObject;

namespace speecher::mac {

// Calls reopen when macOS asks Speecher to reopen: a click on its Dock icon,
// or opening the app again while it runs. Qt only reports that as the app
// becoming active, which it also does for every other activation. Lasts
// until context is destroyed; reopen runs on the main thread.
void onReopen(QObject *context, std::function<void()> reopen);

} // namespace speecher::mac
