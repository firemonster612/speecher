#pragma once

#include <QString>

namespace speecher {

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

} // namespace speecher
