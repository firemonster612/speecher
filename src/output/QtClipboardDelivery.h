#pragma once

#include <QObject>
#include <QString>

namespace speecher {

struct DeliveryContent;

class QtClipboardDelivery : public QObject {
    Q_OBJECT

public:
    explicit QtClipboardDelivery(QObject *parent = nullptr);
    bool copy(const QString &text, QString *error = nullptr);
    bool copy(const DeliveryContent &content, QString *error = nullptr);
};

} // namespace speecher
