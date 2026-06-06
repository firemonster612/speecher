#include "output/TextDelivery.h"

#include "core/AppSettings.h"
#include "output/ClipboardDelivery.h"
#include "output/WtypeDelivery.h"

namespace speecher {

TextDelivery::TextDelivery(QObject *parent)
    : TextDeliveryAdapter(parent)
{
}

DeliveryResult TextDelivery::deliver(const OutputSettings &settings, const QString &text)
{
    return deliver(settings.typeCommand, text, settings.fallbackClipboard);
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
