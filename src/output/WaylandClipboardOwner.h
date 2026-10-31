#pragma once

#include "output/ClipboardSnapshot.h"

#include <QObject>

#include <functional>

namespace speecher::WaylandClipboardOwner {

// Publishes parts as the Wayland clipboard selection through helper,
// Speecher's clipboard helper, which keeps serving them afterwards. One helper
// owns the selection at a time, process-wide: a new one replaces it, and empty
// parts clear the selection. The one it replaces is let go without waiting,
// and the last one stops as the application quits.
//
// Returns at once. done runs once, when the selection is published or the
// helper failed, unless context is gone by then.
void start(const QString &helper,
           const QList<ClipboardMimePart> &parts,
           QObject *context,
           std::function<void(bool published, const QString &error)> done);

} // namespace speecher::WaylandClipboardOwner
