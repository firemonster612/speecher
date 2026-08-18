#include "output/ClipboardDelivery.h"

#include "dictation/DictationPorts.h"

#include <QApplication>
#include <QClipboard>
#include <QMimeData>

namespace speecher {
namespace {


QClipboard *systemClipboard()
{
    return qApp ? QApplication::clipboard() : nullptr;
}

// QClipboard is backed by NSPasteboard on macOS and by the compositor's data
// device elsewhere, so it is the snapshot of last resort wherever the
// wl-clipboard helpers are missing.
bool captureQtClipboard(ClipboardSnapshot *snapshot, QString *error)
{
    if (!snapshot) {
        if (error) {
            *error = QStringLiteral("No clipboard snapshot destination");
        }
        return false;
    }
    *snapshot = {};

    QClipboard *clipboard = systemClipboard();
    if (!clipboard) {
        if (error) {
            *error = QStringLiteral("Clipboard is unavailable");
        }
        return false;
    }

    const QMimeData *mime = clipboard->mimeData(QClipboard::Clipboard);
    if (!mime) {
        return true;
    }
    // Every advertised format, not just text: restoring less than the owner
    // offered would silently drop images or app-private data.
    for (const QString &format : mime->formats()) {
        const QByteArray data = mime->data(format);
        if (!data.isEmpty()) {
            snapshot->parts.append({format, data});
        }
    }
    if (!snapshot->parts.isEmpty()) {
        snapshot->hasData = true;
        snapshot->mimeType = snapshot->parts.first().mimeType;
        snapshot->data = snapshot->parts.first().data;
    }
    return true;
}

bool restoreQtClipboard(const ClipboardSnapshot &snapshot, QString *error)
{
    QClipboard *clipboard = systemClipboard();
    if (!clipboard) {
        if (error) {
            *error = QStringLiteral("Clipboard is unavailable");
        }
        return false;
    }
    if (!snapshot.hasData) {
        clipboard->clear(QClipboard::Clipboard);
        return true;
    }

    const QList<ClipboardMimePart> parts = snapshot.parts.isEmpty()
        ? QList<ClipboardMimePart>{{snapshot.mimeType, snapshot.data}}
        : snapshot.parts;
    auto *mime = new QMimeData;
    for (const ClipboardMimePart &part : parts) {
        mime->setData(part.mimeType.isEmpty() ? QStringLiteral("application/octet-stream")
                                              : part.mimeType,
                      part.data);
    }
    clipboard->setMimeData(mime, QClipboard::Clipboard);
    return true;
}

} // namespace

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
    if (WlClipboardDelivery::canSnapshot()) {
        return true;
    }
#endif
    return systemClipboard() != nullptr;
}

bool ClipboardDelivery::capture(ClipboardSnapshot *snapshot, QString *error) const
{
#ifdef SPEECHER_WITH_WAYLAND
    if (WlClipboardDelivery::canSnapshot()) {
        return WlClipboardDelivery::capture(snapshot, error);
    }
#endif
    return captureQtClipboard(snapshot, error);
}

bool ClipboardDelivery::restore(const ClipboardSnapshot &snapshot, QString *error) const
{
#ifdef SPEECHER_WITH_WAYLAND
    if (WlClipboardDelivery::canSnapshot()) {
        return WlClipboardDelivery::restore(snapshot, error);
    }
#endif
    return restoreQtClipboard(snapshot, error);
}

} // namespace speecher
