#pragma once

#include "core/DeliverySteps.h"

#include <QString>
#include <QStringList>

// What each Delivery Step needs from this computer before it can run, by
// the names the Pasting page, the setup assistant and health show, and
// whether the clipboard helpers among them are there. It asks the same
// adapters a delivery uses, so what the settings say a step waits for is
// what a delivery meets.

namespace speecher {

namespace DeliveryNeed {
// Desktop accessibility: inserting, and on macOS pressing ⌘V.
inline const QString Accessibility = QStringLiteral("accessibility");
// The virtual keyboard that presses the paste keys on Linux.
inline const QString VirtualKeyboard = QStringLiteral("virtualKeyboard");
// In a Wayland session, Speecher's clipboard helper or wl-copy, to copy.
inline const QString Clipboard = QStringLiteral("clipboard");
// In a Wayland session, wl-paste, to save the clipboard and put it back.
inline const QString ClipboardSave = QStringLiteral("clipboardSave");
} // namespace DeliveryNeed

// Whether the clipboard takes helpers: in a Wayland session Qt's clipboard
// can't copy or save for an app in the background. Elsewhere it does both.
bool clipboardNeedsHelpers();

// What step needs here with these steps: Insert accessibility, except on
// Windows; Press accessibility on macOS, the virtual keyboard on Linux and
// nothing more on Windows. Where the clipboard needs helpers, Press needs
// the clipboard, and saving it with Keep off, which borrows the clipboard;
// Keep needs the clipboard, or with Keep off saving it, to put the previous
// one back. waylandClipboard is clipboardNeedsHelpers, which tests set.
QStringList deliveryNeeds(DeliveryStep step, const DeliverySteps &steps,
                          bool waylandClipboard = clipboardNeedsHelpers());

// Whether the helper a Clipboard or ClipboardSave need names is there;
// always outside a Wayland build.
bool clipboardHelperReady(const QString &need);

// Whether a copy keeps its formatting: where the clipboard needs helpers,
// only Speecher's own offers HTML.
bool clipboardCopiesFormatted();

} // namespace speecher
