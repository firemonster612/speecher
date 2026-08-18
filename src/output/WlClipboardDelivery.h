#pragma once

#include "output/ClipboardSnapshot.h"

#include <QObject>
#include <QString>

#include <memory>

namespace speecher {

struct DeliveryContent;
class WaylandClipboardOwner;

class WlClipboardDelivery : public QObject {
    Q_OBJECT

public:
    explicit WlClipboardDelivery(QObject *parent = nullptr);
    ~WlClipboardDelivery() override;
    bool copy(const DeliveryContent &content, bool *htmlAvailable = nullptr,
              QString *error = nullptr);
    static bool isAvailable();
    static bool isWaylandSession();
    static bool canSnapshot();
    static bool readText(QString *text, QString *error = nullptr);
    static bool capture(ClipboardSnapshot *snapshot, QString *error = nullptr);
    static bool restore(const ClipboardSnapshot &snapshot, QString *error = nullptr);

private:
    std::unique_ptr<WaylandClipboardOwner> m_owner;
};

} // namespace speecher
