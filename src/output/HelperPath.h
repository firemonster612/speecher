#pragma once

#include <QCoreApplication>
#include <QFileInfo>
#include <QString>

namespace speecher {

inline QString resolvedHelperPath(const char *installedPath,
                                  const QString &applicationDir = QCoreApplication::applicationDirPath())
{
    const QString fileName = QFileInfo(QString::fromLatin1(installedPath)).fileName();
    const auto usablePath = [](const QString &path) {
        const QFileInfo file(path);
        return file.isFile() && file.isExecutable() ? file.canonicalFilePath() : QString();
    };

    const QString siblingPath = usablePath(applicationDir + QStringLiteral("/") + fileName);
    if (!siblingPath.isEmpty()) {
        return siblingPath;
    }
    const QString bundledPath = usablePath(
        applicationDir + QStringLiteral("/../libexec/speecher/") + fileName);
    return bundledPath.isEmpty() ? QString::fromLatin1(installedPath) : bundledPath;
}

} // namespace speecher
