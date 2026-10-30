#include "platform/PlasmaDesktopColors.h"

#include <QFileInfo>
#include <QStandardPaths>

namespace speecher {

PlasmaDesktopColors::PlasmaDesktopColors(QObject *parent)
    : DesktopColors(parent)
{
    // KConfig saves kdeglobals by replacing it, and the one in effect may be
    // made later or appear ahead of the system's, so the folders that may
    // hold it are watched as well as the file.
    for (const QString &folder : QStandardPaths::standardLocations(QStandardPaths::GenericConfigLocation)) {
        if (QFileInfo(folder).isDir()) {
            m_watcher.addPath(folder);
        }
    }
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &PlasmaDesktopColors::read);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &PlasmaDesktopColors::read);
    read();
}

void PlasmaDesktopColors::read()
{
    // Read on every change in the config folder: two saves within one tick
    // of the file system's clock leave kdeglobals' time as it was.
    const QString kdeglobals = QStandardPaths::locate(QStandardPaths::GenericConfigLocation, QStringLiteral("kdeglobals"));
    // Replacing the file drops it from the watcher.
    if (!kdeglobals.isEmpty() && !m_watcher.files().contains(kdeglobals)) {
        m_watcher.addPath(kdeglobals);
    }
    m_scheme = readKdeColorScheme(kdeglobals);
    emit changed();
}

} // namespace speecher
