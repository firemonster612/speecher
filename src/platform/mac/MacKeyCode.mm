#include "platform/mac/MacKeyCode.h"

#include <QString>

#import <Carbon/Carbon.h>

namespace speecher::mac {

std::optional<quint16> keyCodeForCharacter(QChar character, Qt::KeyboardModifiers modifiers)
{
    TISInputSourceRef source = TISCopyCurrentKeyboardLayoutInputSource();
    if (!source) return std::nullopt;
    CFDataRef data = static_cast<CFDataRef>(
        TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData));
    std::optional<quint16> result;
    if (data) {
        const auto *layout = reinterpret_cast<const UCKeyboardLayout *>(CFDataGetBytePtr(data));
        const QString wanted = QString(character).toUpper();
        // Option changes the produced text, but QKeySequence names the base key.
        // Command is different: some layouts explicitly remap command chords.
        const UInt32 translationModifiers =
            ((modifiers.testFlag(Qt::ShiftModifier) ? shiftKey : 0)
             | (modifiers.testFlag(Qt::ControlModifier) ? cmdKey : 0)) >> 8;
        for (UInt16 code = 0; code < 128; ++code) {
            // QKeySequence can name either Shift+1 or Shift+!. Match the base
            // character too, without accidentally preferring the numeric pad.
            for (const UInt32 flags : {translationModifiers,
                                        translationModifiers & ~(shiftKey >> 8)}) {
                UInt32 deadKeyState = 0;
                UniChar characters[4];
                UniCharCount length = 0;
                if (UCKeyTranslate(layout, code, kUCKeyActionDisplay, flags,
                                   LMGetKbdType(), kUCKeyTranslateNoDeadKeysMask, &deadKeyState,
                                   4, &length, characters) == noErr
                    && QString::fromUtf16(reinterpret_cast<const char16_t *>(characters), length)
                           .toUpper() == wanted) {
                    result = code;
                    break;
                }
            }
            if (result) break;
        }
    }
    CFRelease(source);
    return result;
}

} // namespace speecher::mac
