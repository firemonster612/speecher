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

// What a copy, a snapshot or a restore said once it finished.
struct ClipboardCopied {
    bool ok = false;
    // The HTML went on the clipboard along with the plain text.
    bool htmlAvailable = false;
    QString error;
};

struct ClipboardCaptured {
    bool ok = false;
    ClipboardSnapshot snapshot;
    QString error;
};

struct ClipboardRestored {
    bool ok = false;
    // Someone else's newer copy was left in place, as asked.
    bool keptNewerCopy = false;
    QString error;
};

} // namespace speecher
