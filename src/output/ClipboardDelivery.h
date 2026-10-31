#pragma once

#include "output/ClipboardSnapshot.h"
#include "output/QtClipboardDelivery.h"
#ifdef SPEECHER_WITH_WAYLAND
#include "output/WlClipboardDelivery.h"
#endif

#include <QObject>
#include <QString>

#include <functional>

namespace speecher {

struct DeliveryContent;

class ClipboardDelivery : public QObject {
    Q_OBJECT

public:
    explicit ClipboardDelivery(QObject *parent = nullptr);
    // Each of these answers through done once, unless context is gone by
    // then. Today all answer before they return; callers must not rely on it,
    // as the wl-clipboard helpers are to answer once they finish.
    void copy(const DeliveryContent &content, QObject *context, std::function<void(const ClipboardCopied &)> done);
#ifdef SPEECHER_WITH_WAYLAND
    void copyWayland(const DeliveryContent &content,
                     QObject *context,
                     std::function<void(const ClipboardCopied &)> done);
#endif
    void copyQt(const DeliveryContent &content, QObject *context, std::function<void(const ClipboardCopied &)> done);
    // A snapshot preserves as much of the previous clipboard as the available
    // backend can read: every offered format through wl-clipboard, plain text
    // and HTML through QClipboard.
    bool canSnapshot() const;
    void capture(QObject *context, std::function<void(const ClipboardCaptured &)> done);
    // With preserveNewCopy, a clipboard that someone else has since claimed is
    // left alone and keptNewerCopy is said; that still counts as restored.
    // preserveNewCopy consumes the record of what this object last copied, so
    // the question cannot be asked twice for one copy.
    void restore(const ClipboardSnapshot &snapshot,
                 bool preserveNewCopy,
                 QObject *context,
                 std::function<void(const ClipboardRestored &)> done);

private:
    QtClipboardDelivery m_qtClipboard;
#ifdef SPEECHER_WITH_WAYLAND
    WlClipboardDelivery m_waylandClipboard;
#endif
};

} // namespace speecher
