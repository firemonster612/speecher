#include "core/PasteMethod.h"

namespace speecher {

QString pasteMethodName(PasteMethod method)
{
    switch (method) {
    case PasteMethod::TerminalPaste:
        return QStringLiteral("terminal_paste");
    case PasteMethod::DirectInsert:
        return QStringLiteral("direct_insert");
    case PasteMethod::ClipboardOnly:
        return QStringLiteral("clipboard_only");
    case PasteMethod::StandardPaste:
        return QStringLiteral("standard_paste");
    }
    return QStringLiteral("standard_paste");
}

PasteMethod pasteMethodFromName(const QString &name)
{
    if (name == QStringLiteral("terminal_paste")) {
        return PasteMethod::TerminalPaste;
    }
    if (name == QStringLiteral("direct_insert")) {
        return PasteMethod::DirectInsert;
    }
    if (name == QStringLiteral("clipboard_only")) {
        return PasteMethod::ClipboardOnly;
    }
    return PasteMethod::StandardPaste;
}

} // namespace speecher
