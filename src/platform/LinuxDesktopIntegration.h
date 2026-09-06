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

// Where installed AppImages live: ~/Applications, or ~/AppImages when only
// that already exists.
QString appImageInstallDirectory(const QString &homePath);

// Whether the image already sits in one of the recognized install folders.
bool appImageInInstallFolder(const QString &homePath, const QString &appImagePath);

// Moves the AppImage into appImageInstallDirectory() and writes the resulting
// path to *installedPath. Leaves an image already in either recognized folder
// where it is. The running process keeps working after the move: the mounted
// filesystem holds the old file open, only the path changes.
bool relocateAppImage(const QString &homePath,
                      const QString &appImagePath,
                      QString *installedPath,
                      QString *error = nullptr);

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
// is removed only when its target is the running AppImage, or when APPIMAGE is
// unavailable and the target has a Speecher AppImage name.
DesktopIntegrationRemoval removeAppImageIntegration(const QString &homePath);

} // namespace speecher
