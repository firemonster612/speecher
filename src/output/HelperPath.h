#pragma once

#include <QCoreApplication>
#include <QFileInfo>
#include <QString>

namespace speecher {

inline QString resolvedHelperPath(const char *installedPath,
                                  const QString &applicationDir = QCoreApplication::applicationDirPath())
{
    const QString fileName = QFileInfo(QString::fromLatin1(installedPath)).fileName();
    const QString siblingPath = QFileInfo(applicationDir + QStringLiteral("/") + fileName).canonicalFilePath();
    if (!siblingPath.isEmpty()) {
        return siblingPath;
    }
    const QString bundledPath = QFileInfo(
        applicationDir + QStringLiteral("/../libexec/speecher/") + fileName).canonicalFilePath();
    return bundledPath.isEmpty() ? QString::fromLatin1(installedPath) : bundledPath;
}

} // namespace speecher
