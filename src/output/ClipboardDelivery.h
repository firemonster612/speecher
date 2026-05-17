#pragma once

#include <QObject>
#include <QString>

namespace speecher {

class ClipboardDelivery : public QObject {
    Q_OBJECT

public:
    explicit ClipboardDelivery(QObject *parent = nullptr);
    bool copy(const QString &text, QString *error = nullptr);
};

} // namespace speecher
