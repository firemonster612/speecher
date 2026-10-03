#pragma once

#include <QString>

#include <functional>

#include <windows.h>

class QObject;

namespace speecher::win {

// Raw keyboard input is registered per process: Windows sends every WM_INPUT
// to the one window registered last. The binders that watch keys (each Global
// Shortcut's combination release and single key) listen here instead of
// registering windows of their own, which would take the stream from each
// other. Delivers every keyboard RAWINPUT to listener until context is
// destroyed or listens again; false, with error, when Windows refuses.
bool listenToRawKeyboard(QObject *context,
                         std::function<void(const RAWINPUT &)> listener,
                         QString *error);

} // namespace speecher::win
