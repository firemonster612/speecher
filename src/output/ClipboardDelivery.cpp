#include "output/ClipboardDelivery.h"

#include "output/QtClipboardDelivery.h"
#include "output/WlClipboardDelivery.h"

namespace speecher {

ClipboardDelivery::ClipboardDelivery(QObject *parent)
    : QObject(parent)
{
}

bool ClipboardDelivery::copy(const QString &text, QString *error)
{
    QString wlCopyError;
    WlClipboardDelivery wlCopy;
    if (wlCopy.copy(text, &wlCopyError)) {
        return true;
    }

    QtClipboardDelivery qtClipboard;
    QString qtError;
    if (qtClipboard.copy(text, &qtError)) {
        return true;
    }

    if (error) {
        *error = wlCopyError.isEmpty() ? qtError : wlCopyError;
    }
    return false;
}

} // namespace speecher
