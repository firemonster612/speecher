#pragma once

#include <QObject>
#include <QString>

namespace speecher {

struct DeliveryResult {
    bool ok = false;
    bool copied = false;
    QString message;
};

class TextDelivery : public QObject {
    Q_OBJECT

public:
    explicit TextDelivery(QObject *parent = nullptr);
    DeliveryResult deliver(const QString &command, const QString &text, bool allowClipboardFallback);
};

} // namespace speecher
