#pragma once

#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>

// The apps installed on this computer, as the Apps page's picker lists them:
// each with its name, an icon, and every id a Target may report for it, which
// an App's Id matches use. Each platform finds them its own way:
// LinuxInstalledApps.cpp, mac/MacInstalledApps.mm, win/WinInstalledApps.cpp.

namespace speecher {

struct InstalledApp {
    QString name;
    // The key installedAppIcon draws: a theme icon name or an image path on
    // Linux, the bundle's path on macOS, the Start menu item's parsing name
    // on Windows. Empty where the app has none.
    QString icon;
    // Every identifier a Target may report for the app, first the one it is
    // best known by: a desktop file id, a bundle id, or a program name.
    QStringList ids;
    // Where it was found: a .desktop file, an .app bundle or a Start menu
    // item.
    QString location;

    bool operator==(const InstalledApp &other) const = default;
};

// Sorted by name. Reads the disk, so it is slow: call it off the UI thread.
QList<InstalledApp> installedApps();
// The icon `key` names, at about size × size pixels, or a null image where
// there is none. Called from the icon provider's thread.
QImage installedAppIcon(const QString &key, int size);

#ifdef Q_OS_LINUX
// The apps the .desktop files under applicationDirs describe, as the desktop
// entry spec reads them: a file id found in an earlier directory hides the
// same id later, and an entry that isn't an application, is hidden or not
// for one of currentDesktops (as $XDG_CURRENT_DESKTOP lists them), or whose
// TryExec isn't installed, is left out.
QList<InstalledApp> desktopEntryApps(const QStringList &applicationDirs, const QStringList &currentDesktops);
#endif

} // namespace speecher
