#include "output/TextDelivery.h"

#include "output/ClipboardDelivery.h"
#include "output/WtypeDelivery.h"

namespace speecher {

TextDelivery::TextDelivery(QObject *parent)
    : QObject(parent)
{
}

DeliveryResult TextDelivery::deliver(const QString &command, const QString &text, bool allowClipboardFallback)
{
    QString error;
    WtypeDelivery wtype;
    if (wtype.deliver(command, text, &error)) {
        return {true, false, QStringLiteral("Delivered")};
    }
    if (!allowClipboardFallback) {
        return {false, false, error};
    }

    ClipboardDelivery clipboard;
    QString clipboardError;
    if (clipboard.copy(text, &clipboardError)) {
        return {true, true, QStringLiteral("Copied")};
    }
    return {false, false, error.isEmpty() ? clipboardError : error};
}

} // namespace speecher
