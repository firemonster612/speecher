#include "output/ClipboardDelivery.h"

#include "dictation/DictationPorts.h"

#include <QGuiApplication>
#include <QClipboard>
#include <QMimeData>
#include <QPointer>

namespace speecher {
namespace {

// done, kept from running once context is gone. The wl-clipboard helpers
// answer to this delivery's own lifetime, and this to the caller's.
template <typename Answer>
std::function<void(const Answer &)> whileAlive(QObject *context, std::function<void(const Answer &)> done)
{
    return [context = QPointer<QObject>(context), done = std::move(done)](const Answer &answer) {
        if (context) {
            done(answer);
        }
    };
}

QClipboard *systemClipboard()
{
    return qApp ? QGuiApplication::clipboard() : nullptr;
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

void ClipboardDelivery::copy(const DeliveryContent &content,
                             QObject *context,
                             std::function<void(const ClipboardCopied &)> done)
{
#ifdef SPEECHER_WITH_WAYLAND
    if (WlClipboardDelivery::isWaylandSession()) {
        copyWayland(content, context, std::move(done));
        return;
    }
#endif
    copyQt(content, context, std::move(done));
}

#ifdef SPEECHER_WITH_WAYLAND
void ClipboardDelivery::copyWayland(const DeliveryContent &content,
                                    QObject *context,
                                    std::function<void(const ClipboardCopied &)> done)
{
    m_waylandClipboard.copy(content, this, whileAlive(context, std::move(done)));
}
#endif

void ClipboardDelivery::copyQt(const DeliveryContent &content,
                               QObject *context,
                               std::function<void(const ClipboardCopied &)> done)
{
    Q_UNUSED(context)
    QString error;
    const bool ok = m_qtClipboard.copy(content, &error);
    done({ok, ok && content.html.has_value(), error});
}

bool ClipboardDelivery::canSnapshot() const
{
#ifdef SPEECHER_WITH_WAYLAND
    // On Wayland, QClipboard only sees a selection its own client received a
    // data-device event for, so a background client reads nothing without
    // wl-paste. Reporting that empty read as a real snapshot is how restore
    // ends up wiping a clipboard that was never empty, so a Wayland session
    // must answer through wl-paste's availability, never through Qt.
    if (WlClipboardDelivery::isWaylandSession()) {
        return WlClipboardDelivery::canSnapshot();
    }
#endif
    return systemClipboard() != nullptr;
}

void ClipboardDelivery::capture(QObject *context, std::function<void(const ClipboardCaptured &)> done)
{
#ifdef SPEECHER_WITH_WAYLAND
    if (WlClipboardDelivery::isWaylandSession()) {
        WlClipboardDelivery::capture(this, whileAlive(context, std::move(done)));
        return;
    }
#endif
    Q_UNUSED(context)
    ClipboardCaptured captured;
    captured.ok = captureQtClipboard(&captured.snapshot, &captured.error);
    done(captured);
}

void ClipboardDelivery::restore(const ClipboardSnapshot &snapshot,
                                bool preserveNewCopy,
                                QObject *context,
                                std::function<void(const ClipboardRestored &)> done)
{
#ifdef SPEECHER_WITH_WAYLAND
    if (WlClipboardDelivery::isWaylandSession()) {
        if (preserveNewCopy) {
            m_waylandClipboard.restorePreservingNewCopy(snapshot, this, whileAlive(context, std::move(done)));
        } else {
            WlClipboardDelivery::restore(snapshot, this, whileAlive(context, std::move(done)));
        }
        return;
    }
#endif
    Q_UNUSED(context)
    if (preserveNewCopy && !m_qtClipboard.ownsClipboardContent()) {
        // Keeping another application's newer copy satisfies automatic restore.
        done({true, true, {}});
        return;
    }
    ClipboardRestored restored;
    restored.ok = restoreQtClipboard(snapshot, &restored.error);
    done(restored);
}

} // namespace speecher
