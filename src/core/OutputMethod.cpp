#include "core/OutputMethod.h"

namespace speecher::OutputMethod {

QStringList all()
{
    return {
        QString::fromLatin1(Automatic),
        QString::fromLatin1(Ydotool),
        QString::fromLatin1(WlCopy),
        QString::fromLatin1(QtClipboard),
    };
}

bool isValid(const QString &method)
{
    return all().contains(method);
}

QString normalized(const QString &method)
{
    if (method == QStringLiteral("clipboard")) {
        return QString::fromLatin1(WlCopy);
    }
    if (method == QStringLiteral("qt")) {
        return QString::fromLatin1(QtClipboard);
    }
    if (method == QStringLiteral("wtype")) {
        return QString::fromLatin1(Automatic);
    }
    return isValid(method) ? method : QString::fromLatin1(Automatic);
}

QString label(const QString &method)
{
    const QString value = normalized(method);
    if (value == QString::fromLatin1(Ydotool)) {
        return QStringLiteral("Type with ydotool paste");
    }
    if (value == QString::fromLatin1(WlCopy)) {
        return QStringLiteral("Copy with wl-copy");
    }
    if (value == QString::fromLatin1(QtClipboard)) {
        return QStringLiteral("Copy with Qt clipboard");
    }
    return QStringLiteral("Automatic");
}

} // namespace speecher::OutputMethod
