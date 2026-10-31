#pragma once

#include "output/ClipboardSnapshot.h"

#include <QObject>
#include <QString>

namespace speecher {

struct DeliveryContent;

// A Wayland session's clipboard, through wl-clipboard and Speecher's helper,
// which a background client needs to read it. ClipboardDelivery uses it only
// in a Wayland session, and QClipboard everywhere else.
class WlClipboardDelivery : public QObject {
    Q_OBJECT

public:
    explicit WlClipboardDelivery(QObject *parent = nullptr);
    bool copy(const DeliveryContent &content, bool *htmlAvailable = nullptr,
              QString *error = nullptr);
    static bool isAvailable();
    static bool isWaylandSession();
    static bool canSnapshot();
    static bool capture(ClipboardSnapshot *snapshot, QString *error = nullptr);
    // Restores the snapshot unless something copied over our dictation in the
    // meantime; sets keptNewerCopy when it deliberately leaves that copy alone.
    // Consumes the record of what was copied: the question is answered once
    // per copy, and a later delivery that copies nothing owns no clipboard.
    bool restorePreservingNewCopy(const ClipboardSnapshot &snapshot, QString *error = nullptr,
                                  bool *keptNewerCopy = nullptr);
    // Whether the parts now on the clipboard are still the copy we published.
    static bool copyStillOnClipboard(const QList<ClipboardMimePart> &copied,
                                     const QList<ClipboardMimePart> &current);
    static bool restore(const ClipboardSnapshot &snapshot, QString *error = nullptr);

private:
    QList<ClipboardMimePart> m_copiedParts;
};

} // namespace speecher
