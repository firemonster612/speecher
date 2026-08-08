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

    QString wlCopyError;
    WlClipboardDelivery wlCopy;
    if (wlCopy.copy(content.plainText, &wlCopyError)) {
        return true;
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
        *error = wlCopyError.isEmpty() ? qtError : wlCopyError;
    }
    return false;
}

} // namespace speecher
