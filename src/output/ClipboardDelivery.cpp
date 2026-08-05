#include "output/ClipboardDelivery.h"

#include "output/DeliveryContent.h"
namespace speecher {

ClipboardDelivery::ClipboardDelivery(QObject *parent)
    : QObject(parent)
{
}

bool ClipboardDelivery::copy(const DeliveryContent &content, bool *htmlAvailable, QString *error)
{
    if (htmlAvailable) {
        *htmlAvailable = false;
    }
    if (WlClipboardDelivery::isWaylandSession()) {
        return copyWayland(content, htmlAvailable, error);
    }

    if (content.html) {
        if (copyQt(content, htmlAvailable, error)) {
            return true;
        }
    }

    QString qtError;
    if (copyQt(content, htmlAvailable, &qtError)) {
        return true;
    }

    if (error) {
        *error = qtError;
    }
    return false;
}

bool ClipboardDelivery::copyWayland(const DeliveryContent &content,
                                    bool *htmlAvailable,
                                    QString *error)
{
    return m_waylandClipboard.copy(content, htmlAvailable, error);
}

bool ClipboardDelivery::copyQt(const DeliveryContent &content,
                               bool *htmlAvailable,
                               QString *error)
{
    if (!m_qtClipboard.copy(content, error)) {
        return false;
    }
    if (htmlAvailable) {
        *htmlAvailable = content.html.has_value();
    }
    return true;
}

bool ClipboardDelivery::canSnapshot() const
{
    return WlClipboardDelivery::canSnapshot();
}

bool ClipboardDelivery::capture(WlClipboardSnapshot *snapshot, QString *error) const
{
    return WlClipboardDelivery::capture(snapshot, error);
}

bool ClipboardDelivery::restore(const WlClipboardSnapshot &snapshot, QString *error) const
{
    return WlClipboardDelivery::restore(snapshot, error);
}

} // namespace speecher
