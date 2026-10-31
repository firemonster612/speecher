#pragma once

#include "output/ClipboardSnapshot.h"

#include <QObject>
#include <QString>

#include <functional>

namespace speecher {

struct DeliveryContent;

// A Wayland session's clipboard, through wl-clipboard and Speecher's helper,
// which a background client needs to read it. ClipboardDelivery uses it only
// in a Wayland session, and QClipboard everywhere else.
class WlClipboardDelivery : public QObject {
    Q_OBJECT

public:
    explicit WlClipboardDelivery(QObject *parent = nullptr);
    // Each of these starts the helpers and returns at once. done runs once
    // with their answer, unless context is gone by then.
    void copy(const DeliveryContent &content, QObject *context, std::function<void(const ClipboardCopied &)> done);
    static bool isAvailable();
    static bool isWaylandSession();
    static bool canSnapshot();
    static void capture(QObject *context, std::function<void(const ClipboardCaptured &)> done);
    // Restores the snapshot unless something copied over our dictation in the
    // meantime; says keptNewerCopy when it deliberately leaves that copy alone.
    // Consumes the record of what was copied: the question is answered once
    // per copy, and a later delivery that copies nothing owns no clipboard.
    void restorePreservingNewCopy(const ClipboardSnapshot &snapshot,
                                  QObject *context,
                                  std::function<void(const ClipboardRestored &)> done);
    // Whether the parts now on the clipboard are still the copy we published.
    static bool copyStillOnClipboard(const QList<ClipboardMimePart> &copied,
                                     const QList<ClipboardMimePart> &current);
    static void restore(const ClipboardSnapshot &snapshot,
                        QObject *context,
                        std::function<void(const ClipboardRestored &)> done);

private:
    QList<ClipboardMimePart> m_copiedParts;
};

} // namespace speecher
