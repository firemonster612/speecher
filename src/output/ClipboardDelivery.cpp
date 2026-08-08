#include "output/ClipboardDelivery.h"

#include "output/DeliveryContent.h"
#include "output/QtClipboardDelivery.h"
#include "output/WlClipboardDelivery.h"

namespace speecher {

ClipboardDelivery::ClipboardDelivery(QObject *parent)
    : QObject(parent)
{
}

bool ClipboardDelivery::copy(const QString &text, QString *error)
{
    return copy(DeliveryContent{text, std::nullopt}, nullptr, error);
}

bool ClipboardDelivery::copy(const DeliveryContent &content, bool *htmlAvailable, QString *error)
{
    if (htmlAvailable) {
        *htmlAvailable = false;
    }
    if (WlClipboardDelivery::isWaylandSession()) {
        WlClipboardDelivery waylandClipboard;
        QString waylandError;
        if (waylandClipboard.copy(content, htmlAvailable, &waylandError)) {
            return true;
        }
        if (error) {
            *error = waylandError;
        }
        return false;
    }

    if (content.html) {
        QtClipboardDelivery qtClipboard;
        QString qtError;
        if (qtClipboard.copy(content, error ? error : &qtError)) {
            if (htmlAvailable) {
                *htmlAvailable = true;
            }
            return true;
        }
    }

    QtClipboardDelivery qtClipboard;
    QString qtError;
    if (qtClipboard.copy(content, &qtError)) {
        if (htmlAvailable) {
            *htmlAvailable = content.html.has_value();
        }
        return true;
    }

    if (error) {
        *error = qtError;
    }
    return false;
}

} // namespace speecher
