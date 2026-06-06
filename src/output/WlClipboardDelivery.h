#pragma once

#include <QObject>
#include <QString>

namespace speecher {

class WlClipboardDelivery : public QObject {
    Q_OBJECT

public:
    explicit WlClipboardDelivery(QObject *parent = nullptr);
    bool copy(const QString &text, QString *error = nullptr);
    static bool isAvailable();
};

} // namespace speecher
