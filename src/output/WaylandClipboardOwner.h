#pragma once

#include "output/ClipboardSnapshot.h"

namespace speecher::WaylandClipboardOwner {

// Publishes parts as the Wayland clipboard selection through Speecher's
// clipboard helper, which keeps serving them after this returns. One helper
// owns the selection at a time, process-wide: a new one replaces it, and empty
// parts clear the selection. The last one stops as the application quits.
bool start(const QList<ClipboardMimePart> &parts, QString *error = nullptr);

} // namespace speecher::WaylandClipboardOwner
