#include "output/mac/MacPasteDelivery.h"

#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>

namespace speecher {
namespace {

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

} // namespace speecher
