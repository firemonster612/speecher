#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace speecher::WaylandClipboardProcess {

QString wlCopyExecutable();
QString wlPasteExecutable();
QString helperExecutable();
bool run(const QString &executable,
         const QString &tool,
         const QStringList &arguments,
         const QByteArray *input,
         QByteArray *output,
         QString *error);
bool looksLikeEmptyClipboardError(const QString &message);

} // namespace speecher::WaylandClipboardProcess
