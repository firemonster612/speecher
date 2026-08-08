#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QList>

#include <memory>

namespace speecher {

struct DeliveryContent;
class WaylandClipboardOwner;

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
    ~WlClipboardDelivery() override;
    bool copy(const DeliveryContent &content, bool *htmlAvailable = nullptr,
              QString *error = nullptr);
    static bool isAvailable();
    static bool isWaylandSession();
    static bool canSnapshot();
    static bool readText(QString *text, QString *error = nullptr);
    static bool capture(WlClipboardSnapshot *snapshot, QString *error = nullptr);
    static bool restore(const WlClipboardSnapshot &snapshot, QString *error = nullptr);

private:
    std::unique_ptr<WaylandClipboardOwner> m_owner;
};

} // namespace speecher
