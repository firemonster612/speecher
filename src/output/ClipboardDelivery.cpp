#include "output/ClipboardDelivery.h"

#include "dictation/DictationPorts.h"
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
#ifdef SPEECHER_WITH_WAYLAND
    if (WlClipboardDelivery::isWaylandSession()) {
        return copyWayland(content, htmlAvailable, error);
    }
#endif

    QString qtError;
    if (copyQt(content, htmlAvailable, &qtError)) {
        return true;
    }

    if (error) {
        *error = qtError;
    }
    return false;
}

#ifdef SPEECHER_WITH_WAYLAND
bool ClipboardDelivery::copyWayland(const DeliveryContent &content,
                                    bool *htmlAvailable,
                                    QString *error)
{
    return m_waylandClipboard.copy(content, htmlAvailable, error);
}
#endif

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
#ifdef SPEECHER_WITH_WAYLAND
    return WlClipboardDelivery::canSnapshot();
#else
    return false;
#endif
}

bool ClipboardDelivery::capture(WlClipboardSnapshot *snapshot, QString *error) const
{
#ifdef SPEECHER_WITH_WAYLAND
    return WlClipboardDelivery::capture(snapshot, error);
#else
    Q_UNUSED(snapshot)
    Q_UNUSED(error)
    return false;
#endif
}

bool ClipboardDelivery::restore(const WlClipboardSnapshot &snapshot, QString *error) const
{
#ifdef SPEECHER_WITH_WAYLAND
    return WlClipboardDelivery::restore(snapshot, error);
#else
    Q_UNUSED(snapshot)
    Q_UNUSED(error)
    return false;
#endif
}

} // namespace speecher
