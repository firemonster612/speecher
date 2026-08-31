#pragma once

#include "output/ClipboardSnapshot.h"
#include "output/QtClipboardDelivery.h"
#ifdef SPEECHER_WITH_WAYLAND
#include "output/WlClipboardDelivery.h"
#endif

#include <QObject>
#include <QString>

namespace speecher {

struct DeliveryContent;

class ClipboardDelivery : public QObject {
    Q_OBJECT

public:
    explicit ClipboardDelivery(QObject *parent = nullptr);
    bool copy(const DeliveryContent &content, bool *htmlAvailable = nullptr, QString *error = nullptr);
#ifdef SPEECHER_WITH_WAYLAND
    bool copyWayland(const DeliveryContent &content,
                     bool *htmlAvailable = nullptr,
                     QString *error = nullptr);
#endif
    bool copyQt(const DeliveryContent &content, bool *htmlAvailable = nullptr, QString *error = nullptr);
    // A snapshot preserves as much of the previous clipboard as the available
    // backend can read: every offered format through wl-clipboard, plain text
    // and HTML through QClipboard.
    bool canSnapshot() const;
    bool capture(ClipboardSnapshot *snapshot, QString *error = nullptr) const;
    bool restore(const ClipboardSnapshot &snapshot, QString *error = nullptr) const;

private:
    QtClipboardDelivery m_qtClipboard;
#ifdef SPEECHER_WITH_WAYLAND
    WlClipboardDelivery m_waylandClipboard;
#endif
};

} // namespace speecher
