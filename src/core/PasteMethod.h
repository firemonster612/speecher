#pragma once

#include <QString>

// How an App or App Type narrows the Delivery Steps (its Pasting choice), and
// the names it is stored by.

namespace speecher {

enum class PasteMethod {
    // The steps as they are.
    StandardPaste,
    // The steps, pressing a terminal's paste keys.
    TerminalPaste,
    // The steps without the paste keys.
    DirectInsert,
    ClipboardOnly,
};

QString pasteMethodName(PasteMethod method);
PasteMethod pasteMethodFromName(const QString &name);
// Whether a terminal's paste keys differ from everyone else's. macOS presses
// ⌘V everywhere, so Terminal paste means nothing there.
#ifdef Q_OS_MACOS
inline constexpr bool terminalKeysDiffer = false;
#else
inline constexpr bool terminalKeysDiffer = true;
#endif

} // namespace speecher
