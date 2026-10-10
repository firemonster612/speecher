#include "app/CommandLineTool.h"

#include "app/CommandLineToolPresentation.h"

#include <QFileInfo>

namespace speecher {

namespace {

// An AppleScript string escapes only backslashes and double quotes.
QString appleScriptString(QString value)
{
    value.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QStringLiteral("\"") + value + QStringLiteral("\"");
}

} // namespace

QString shellQuote(QString value)
{
    return QStringLiteral("'")
        + value.replace(QLatin1Char('\''), QStringLiteral("'\"'\"'"))
        + QStringLiteral("'");
}

QString commandLineToolInstallCommand(const QString &binaryPath, const QString &toolPath)
{
    const QString folder = QFileInfo(toolPath).path();
    const QString exec = QStringLiteral("exec %1 \"$@\"").arg(shellQuote(binaryPath));
    // printf writes the newlines, so the command has none of its own and fits
    // in one AppleScript string. The script is written beside toolPath and
    // renamed onto it: the rename replaces a symlink left by a manual install
    // rather than writing through it, and nothing can swap one in between.
    return QStringLiteral("mkdir -p %1 && tmp=$(mktemp %1/.speecher.XXXXXX) && printf '%s\\n' '#!/bin/sh' %3 > \"$tmp\""
                          " && chmod 755 \"$tmp\" && mv -f \"$tmp\" %2")
        .arg(shellQuote(folder), shellQuote(toolPath), shellQuote(exec));
}

QString commandLineToolInstallScript(const QString &binaryPath, const QString &toolPath,
                                     bool withAdministratorPrivileges)
{
    QString script = QStringLiteral("do shell script ")
        + appleScriptString(commandLineToolInstallCommand(binaryPath, toolPath));
    if (withAdministratorPrivileges) {
        script += QStringLiteral(" with prompt %1 with administrator privileges")
                      .arg(appleScriptString(commandLineToolPasswordPrompt()));
    }
    return script;
}

} // namespace speecher
