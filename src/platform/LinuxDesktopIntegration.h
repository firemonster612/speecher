#pragma once

#include <QString>
#include <QStringList>

namespace speecher {

QString resolvedPath(const QString &path);

// Whether this session is Wayland, which decides the single-key backend and
// whether the key-watch helper is offered.
bool isWaylandSession();

// The program and arguments that open this desktop's keyboard shortcut
// settings, Plasma's or GNOME's, as XDG_CURRENT_DESKTOP names the desktop;
// empty where Speecher knows of none or the program is not installed.
QStringList desktopShortcutSettingsCommand();

// What removal did, in the words a person sees: each entry names the item
// (app menu entry, speecher command, icon) and, for failures, why.
struct DesktopIntegrationRemoval {
    QStringList removed;
    QStringList absent;
    QStringList failed;
};

// Undoes what an earlier release's AppImage install left: the desktop file,
// the icon, the ~/.local/bin/speecher link and the relocated setup helper.
// The command link is removed only when its target is the running AppImage,
// or when APPIMAGE is unavailable and the target has a Speecher AppImage name.
DesktopIntegrationRemoval removeAppImageIntegration(const QString &homePath);

} // namespace speecher
