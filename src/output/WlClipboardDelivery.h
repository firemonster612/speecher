#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QList>

namespace speecher {

struct DeliveryContent;

struct ClipboardMimePart {
    QString mimeType;
    QByteArray data;
};

struct WlClipboardSnapshot {
    bool hasData = false;
    QString mimeType;
    QByteArray data;
    QList<ClipboardMimePart> parts;
};

class WlClipboardDelivery : public QObject {
    Q_OBJECT

public:
    explicit WlClipboardDelivery(QObject *parent = nullptr);
    bool copy(const QString &text, QString *error = nullptr);
    bool copy(const DeliveryContent &content, bool *htmlAvailable = nullptr,
              QString *error = nullptr);
    static bool isAvailable();
    static bool isWaylandSession();
    static bool canSnapshot();
    static bool capture(WlClipboardSnapshot *snapshot, QString *error = nullptr);
    static bool restore(const WlClipboardSnapshot &snapshot, QString *error = nullptr);
};

} // namespace speecher
