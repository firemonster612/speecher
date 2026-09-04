#pragma once

#include <QString>
#include <QStringList>

namespace speecher {

QString resolvedPath(const QString &path);
QString quotedExecutablePath(const QString &path);

QString globalShortcutInstructionCommand(const QString &homePath,
                                         const QString &appImagePath,
                                         const QString &binaryPath);

bool writeAppImageDesktopFile(const QString &sourcePath,
                              const QString &targetPath,
                              const QString &appImagePath,
                              QString *error = nullptr);

bool appImageIntegrationInstalled(const QString &homePath,
                                  const QString &appImagePath);

bool installAppImageIntegration(const QString &homePath,
                                const QString &appImagePath,
                                const QString &applicationDirPath,
                                QString *error = nullptr);

// What removal did, in the words a person sees: each entry names the item
// (app menu entry, speecher command, icon) and, for failures, why.
struct DesktopIntegrationRemoval {
    QStringList removed;
    QStringList absent;
    QStringList failed;
};

// Undoes installAppImageIntegration: the desktop file, the icon and the
// ~/.local/bin/speecher link, and the relocated setup helper. The command link
// is removed only when its target is a Speecher AppImage.
DesktopIntegrationRemoval removeAppImageIntegration(const QString &homePath);

} // namespace speecher
