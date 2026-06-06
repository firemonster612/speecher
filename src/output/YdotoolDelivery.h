#pragma once

#include <QObject>
#include <QString>

namespace speecher {

class YdotoolDelivery : public QObject {
    Q_OBJECT

public:
    explicit YdotoolDelivery(QObject *parent = nullptr);
    bool type(const QString &text, QString *error = nullptr);
    static bool isAvailable();
    static QString socketPath();
    static QStringList commandArguments(const QString &text);
    static QString withoutTrailingWhitespace(const QString &text);
};

} // namespace speecher
