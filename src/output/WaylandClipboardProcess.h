#pragma once

#include <QString>

namespace speecher::WaylandClipboardProcess {

QString wlCopyExecutable();
QString wlPasteExecutable();
QString helperExecutable();
bool looksLikeEmptyClipboardError(const QString &message);

} // namespace speecher::WaylandClipboardProcess
