#include "app/CommandLineTool.h"

#include <QFileInfo>

namespace speecher {

QString shellQuote(QString value)
{
    return QStringLiteral("'")
        + value.replace(QLatin1Char('\''), QStringLiteral("'\"'\"'"))
        + QStringLiteral("'");
}

bool isLastingAppLocation(const QString &binaryPath)
{
    return !binaryPath.startsWith(QStringLiteral("/Volumes/"))
        && !binaryPath.contains(QStringLiteral("/AppTranslocation/"));
}

QString commandLineToolInstallCommand(const QString &binaryPath, const QString &toolPath)
{
    const QString exec = QStringLiteral("exec %1 \"$@\"").arg(shellQuote(binaryPath));
    // printf writes the newlines, so the command has none of its own and fits
    // in one AppleScript string. rm first: redirecting onto a symlink left by
    // a manual install would write through it into whatever it points at.
    return QStringLiteral("mkdir -p %1 && rm -f %2 && printf '%s\\n' '#!/bin/sh' %3 > %2 && chmod 755 %2")
        .arg(shellQuote(QFileInfo(toolPath).path()), shellQuote(toolPath), shellQuote(exec));
}

QString commandLineToolInstallScript(const QString &binaryPath, const QString &toolPath,
                                     bool withAdministratorPrivileges)
{
    // An AppleScript string escapes only backslashes and double quotes.
    QString command = commandLineToolInstallCommand(binaryPath, toolPath);
    command.replace(QLatin1Char('\\'), QStringLiteral("\\\\"))
        .replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QStringLiteral("do shell script \"%1\"%2")
        .arg(command, withAdministratorPrivileges ? QStringLiteral(" with administrator privileges")
                                                  : QString());
}

} // namespace speecher
