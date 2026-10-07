#include "app/CommandLineTool.h"

#include <QFileInfo>

namespace speecher {
namespace {

QString shellQuote(QString value)
{
    return QStringLiteral("'")
        + value.replace(QLatin1Char('\''), QStringLiteral("'\"'\"'"))
        + QStringLiteral("'");
}

} // namespace

QString commandLineToolInstallCommand(const QString &binaryPath, const QString &toolPath)
{
    const QString exec = QStringLiteral("exec %1 \"$@\"").arg(shellQuote(binaryPath));
    // printf writes the newlines, so the command has none of its own and fits
    // in one AppleScript string. rm first: redirecting onto a symlink left by
    // a manual install would write through it into whatever it points at.
    return QStringLiteral("mkdir -p %1 && rm -f %2 && printf '%s\\n' '#!/bin/sh' %3 > %2 && chmod 755 %2")
        .arg(shellQuote(QFileInfo(toolPath).path()), shellQuote(toolPath), shellQuote(exec));
}

} // namespace speecher
