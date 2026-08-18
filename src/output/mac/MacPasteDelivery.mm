#include "output/mac/MacPasteDelivery.h"

#include <QThread>

#include <algorithm>

#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>

namespace speecher {
namespace {

// CGEventKeyboardSetUnicodeString truncates long strings, so text goes out in
// short runs with a pause that gives the receiving app time to consume them.
constexpr qsizetype unicodeChunkLength = 20;
constexpr unsigned long chunkPauseUs = 2000;

QString accessibilityRequiredMessage()
{
    return QStringLiteral(
        "Grant Speecher Accessibility access in System Settings before it can send keystrokes");
}

bool postKeyStroke(CGKeyCode keyCode, CGEventFlags flags, QString *error)
{
    CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    CGEventRef keyDown = CGEventCreateKeyboardEvent(source, keyCode, true);
    CGEventRef keyUp = CGEventCreateKeyboardEvent(source, keyCode, false);
    if (source) {
        CFRelease(source);
    }
    if (!keyDown || !keyUp) {
        if (keyDown) {
            CFRelease(keyDown);
        }
        if (keyUp) {
            CFRelease(keyUp);
        }
        if (error) {
            *error = QStringLiteral("Could not create the paste keystroke");
        }
        return false;
    }

    CGEventSetFlags(keyDown, flags);
    CGEventSetFlags(keyUp, flags);
    CGEventPost(kCGHIDEventTap, keyDown);
    CGEventPost(kCGHIDEventTap, keyUp);
    CFRelease(keyDown);
    CFRelease(keyUp);
    return true;
}

} // namespace

bool MacPasteDelivery::isAvailable()
{
    return AXIsProcessTrusted();
}

bool MacPasteDelivery::paste(QString *error)
{
    if (!isAvailable()) {
        if (error) {
            *error = accessibilityRequiredMessage();
        }
        return false;
    }
    // macOS has one paste chord: Terminal.app and every other terminal take
    // Cmd+V, so PasteMethod::TerminalPaste needs no keystroke of its own here.
    return postKeyStroke(kVK_ANSI_V, kCGEventFlagMaskCommand, error);
}

bool MacPasteDelivery::typeText(const QString &text, QString *error)
{
    if (!isAvailable()) {
        if (error) {
            *error = accessibilityRequiredMessage();
        }
        return false;
    }
    if (text.isEmpty()) {
        return true;
    }

    CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    const UniChar *utf16 = reinterpret_cast<const UniChar *>(text.utf16());
    for (qsizetype offset = 0; offset < text.size(); offset += unicodeChunkLength) {
        const qsizetype length = std::min(unicodeChunkLength, text.size() - offset);
        CGEventRef event = CGEventCreateKeyboardEvent(source, 0, true);
        if (!event) {
            if (source) {
                CFRelease(source);
            }
            if (error) {
                *error = QStringLiteral("Could not create the typing event");
            }
            return false;
        }
        CGEventKeyboardSetUnicodeString(event, static_cast<UniCharCount>(length), utf16 + offset);
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
        QThread::usleep(chunkPauseUs);
    }
    if (source) {
        CFRelease(source);
    }
    return true;
}

} // namespace speecher
