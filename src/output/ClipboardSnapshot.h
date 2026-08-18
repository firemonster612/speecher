#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

namespace speecher {

struct ClipboardMimePart {
    QString mimeType;
    QByteArray data;
};

// What the clipboard held before Speecher overwrote it, so delivery can put it
// back. Every backend fills the same shape, however few formats it can read.
struct ClipboardSnapshot {
    bool hasData = false;
    QString mimeType;
    QByteArray data;
    QList<ClipboardMimePart> parts;
};

} // namespace speecher
