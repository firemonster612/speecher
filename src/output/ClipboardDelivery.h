#pragma once

#include <QObject>
#include <QString>

namespace speecher {

struct DeliveryContent;

class ClipboardDelivery : public QObject {
    Q_OBJECT

public:
    explicit ClipboardDelivery(QObject *parent = nullptr);
    bool copy(const QString &text, QString *error = nullptr);
    bool copy(const DeliveryContent &content, bool *htmlAvailable = nullptr, QString *error = nullptr);
};

} // namespace speecher
