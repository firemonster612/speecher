#include "output/WaylandClipboardProcess.h"

#include "output/HelperPath.h"

#include <QFileInfo>
#include <QStandardPaths>

namespace speecher::WaylandClipboardProcess {

QString wlCopyExecutable()
{
    return QStandardPaths::findExecutable(QStringLiteral("wl-copy"));
}

QString wlPasteExecutable()
{
    return QStandardPaths::findExecutable(QStringLiteral("wl-paste"));
}

QString helperExecutable()
{
#ifdef SPEECHER_WAYLAND_CLIPBOARD_HELPER_PATH
    const QString installed = resolvedHelperPath(SPEECHER_WAYLAND_CLIPBOARD_HELPER_PATH);
    if (QFileInfo::exists(installed) && QFileInfo(installed).isExecutable()) {
        return installed;
    }
#endif
    return QStandardPaths::findExecutable(QStringLiteral("speecher-wayland-clipboard"));
}

bool looksLikeEmptyClipboardError(const QString &message)
{
    const QString lower = message.toLower();
    return lower.contains(QStringLiteral("nothing is copied"))
        || lower.contains(QStringLiteral("clipboard is empty"))
        || lower.contains(QStringLiteral("no selection"))
        || lower.contains(QStringLiteral("no data"));
}

} // namespace speecher::WaylandClipboardProcess
