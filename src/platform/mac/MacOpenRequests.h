#pragma once

#include <functional>

class QObject;
class QString;

namespace speecher::mac {

// Calls reopen when macOS asks Speecher to reopen: a click on its Dock icon,
// or opening the app again while it runs. Qt only reports that as the app
// becoming active, which it also does for every other activation. Lasts
// until context is destroyed; reopen runs on the main thread.
void onReopen(QObject *context, std::function<void()> reopen);

// Calls open with each file macOS asks Speecher to open: audio opened with
// Speecher from Finder, or dropped on its Dock icon. Lasts until context is
// destroyed; open runs on the main thread.
void onOpenFile(QObject *context, std::function<void(const QString &path)> open);

} // namespace speecher::mac
