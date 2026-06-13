#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

namespace speecher {

struct WlClipboardSnapshot {
    bool hasData = false;
    QString mimeType;
    QByteArray data;
};

class WlClipboardDelivery : public QObject {
    Q_OBJECT

public:
    explicit WlClipboardDelivery(QObject *parent = nullptr);
    bool copy(const QString &text, QString *error = nullptr);
    static bool isAvailable();
    static bool canSnapshot();
    static bool capture(WlClipboardSnapshot *snapshot, QString *error = nullptr);
    static bool restore(const WlClipboardSnapshot &snapshot, QString *error = nullptr);
};

} // namespace speecher
