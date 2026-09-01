#pragma once

#include <QCoreApplication>
#include <QFileInfo>
#include <QString>

namespace speecher {

inline QString resolvedHelperPath(const char *installedPath)
{
    const QString bundledPath = QFileInfo(
        QCoreApplication::applicationDirPath()
        + QStringLiteral("/../libexec/speecher/")
        + QFileInfo(QString::fromLatin1(installedPath)).fileName())
                                    .canonicalFilePath();
    return bundledPath.isEmpty() ? QString::fromLatin1(installedPath) : bundledPath;
}

} // namespace speecher
