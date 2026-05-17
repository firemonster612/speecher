#pragma once

#include <QObject>
#include <QString>

namespace speecher {

class WtypeDelivery : public QObject {
    Q_OBJECT

public:
    explicit WtypeDelivery(QObject *parent = nullptr);
    bool deliver(const QString &command, const QString &text, QString *error = nullptr);
    static bool isAvailable(const QString &command);
};

} // namespace speecher
