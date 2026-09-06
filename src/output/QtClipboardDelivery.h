#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>

namespace speecher {

struct DeliveryContent;

class QtClipboardDelivery : public QObject {
    Q_OBJECT

public:
    explicit QtClipboardDelivery(QObject *parent = nullptr);
    bool ownsClipboardContent() const;
    bool copy(const DeliveryContent &content, QString *error = nullptr);
private:
    QByteArray m_copyId;
};

} // namespace speecher
