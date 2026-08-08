#pragma once

#include "output/QtClipboardDelivery.h"
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
    bool copyWayland(const DeliveryContent &content,
                     bool *htmlAvailable = nullptr,
                     QString *error = nullptr);
    bool copyQt(const DeliveryContent &content, bool *htmlAvailable = nullptr, QString *error = nullptr);
    bool canSnapshot() const;
    bool capture(WlClipboardSnapshot *snapshot, QString *error = nullptr) const;
    bool restore(const WlClipboardSnapshot &snapshot, QString *error = nullptr) const;

private:
    QtClipboardDelivery m_qtClipboard;
    WlClipboardDelivery m_waylandClipboard;
};

} // namespace speecher
