#include "core/OutputMethod.h"

namespace speecher::OutputMethod {

QStringList all()
{
    return {
        QString::fromLatin1(Automatic),
        QString::fromLatin1(DirectInsert),
        QString::fromLatin1(Ydotool),
        QString::fromLatin1(WlCopy),
        QString::fromLatin1(MacPaste),
        QString::fromLatin1(WinPaste),
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

// What each method does for the person choosing it. The tools that do it
// (ydotool, wl-copy, the Qt clipboard) are not the choice being made.
QString label(const QString &method)
{
    const QString value = normalized(method);
    if (value == QString::fromLatin1(Ydotool)) {
        return QStringLiteral("Paste with the virtual keyboard");
    }
    // Each platform's name for the feature, as the paste rules name it.
    if (value == QString::fromLatin1(DirectInsert)) {
#ifdef Q_OS_MACOS
        return QStringLiteral("Insert into the text field (Accessibility)");
#elif defined(Q_OS_WIN)
        return QStringLiteral("Insert into the text field (UI Automation)");
#else
        return QStringLiteral("Insert into the text field (desktop accessibility)");
#endif
    }
    if (value == QString::fromLatin1(WlCopy)) {
        return QStringLiteral("Copy to the clipboard only");
    }
    // The paste rules' own words, "Standard paste (Ctrl+V)".
    if (value == QString::fromLatin1(MacPaste)) {
        return QStringLiteral("Standard paste (Cmd+V)");
    }
    if (value == QString::fromLatin1(WinPaste)) {
        return QStringLiteral("Standard paste (Ctrl+V)");
    }
    if (value == QString::fromLatin1(QtClipboard)) {
        return QStringLiteral("Copy to the clipboard only (plain text)");
    }
    return QStringLiteral("Automatic");
}

} // namespace speecher::OutputMethod
