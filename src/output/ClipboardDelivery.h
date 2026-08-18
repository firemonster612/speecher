#pragma once

#include "output/QtClipboardDelivery.h"
// Declares WlClipboardSnapshot, which portable delivery code names even where
// no Wayland backend is compiled in.
#include "output/WlClipboardDelivery.h"

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
    bool canSnapshot() const;
    bool capture(WlClipboardSnapshot *snapshot, QString *error = nullptr) const;
    bool restore(const WlClipboardSnapshot &snapshot, QString *error = nullptr) const;

private:
    QtClipboardDelivery m_qtClipboard;
#ifdef SPEECHER_WITH_WAYLAND
    WlClipboardDelivery m_waylandClipboard;
#endif
};

} // namespace speecher
