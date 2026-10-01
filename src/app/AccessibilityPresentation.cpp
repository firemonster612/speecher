#include "app/AccessibilityPresentation.h"

#include "core/settings/SettingsSchema.h"

namespace speecher {

QString accessibilityActionCaption(bool enabled, bool persistent)
{
#ifdef Q_OS_MACOS
    Q_UNUSED(persistent);
    return enabled ? QString() : accessibilityGrantActionLabel();
#else
    if (!enabled) {
        return QStringLiteral("Turn on accessibility");
    }
    return persistent ? QString() : QStringLiteral("Keep on after login");
#endif
}

QStringList accessibilityCapabilities()
{
    return {QStringLiteral("Paste into the app you are using"),
            QStringLiteral("Read the text around your cursor for context")};
}

QString accessibilityCapabilityStatus(bool allowed)
{
    return allowed ? QStringLiteral("On") : QStringLiteral("Off");
}

QString accessibilitySetupStatus(bool supported, bool enabled, bool persistent)
{
    if (!supported) {
        return QStringLiteral("This Speecher build does not include desktop accessibility support.");
    }
    if (enabled) {
        return persistent ? QStringLiteral("Desktop accessibility is on.")
                          : QStringLiteral("Desktop accessibility is on for this session only.");
    }
    return QStringLiteral("Desktop accessibility is off, so Speecher can copy your dictation but not paste it "
                          "or read context.");
}

QString accessibilitySetupFootnote()
{
    return QStringLiteral("It lets Speecher paste, read the text around your cursor and learn your "
                          "corrections, and nothing else.");
}

QString accessibilityNoticeText(bool enabled, bool compact)
{
#ifdef Q_OS_MACOS
    Q_UNUSED(enabled);
    return compact ? QStringLiteral("Without Accessibility, dictation only reaches the clipboard.")
                   : QStringLiteral("Accessibility is off, so Speecher can only leave your dictation on the "
                                    "clipboard. Allow Speecher under Privacy & Security, then restart it. If the "
                                    "toggle already shows Speecher on, turn it off and on again: an updated copy "
                                    "of Speecher does not inherit the old grant.");
#else
    if (!enabled) {
        return compact ? QStringLiteral("Desktop accessibility is off. Pasting into the right app and editing "
                                        "selected text need it.")
                       : QStringLiteral("Desktop accessibility is off, so Speecher cannot tell which app you are "
                                        "in, paste into it, edit selected text, or learn corrections.");
    }
    return compact ? QStringLiteral("Desktop accessibility is on only for this session.")
                   : QStringLiteral("Desktop accessibility is on only for this session. Keep it on after login so "
                                    "these features keep working after you sign in again.");
#endif
}

} // namespace speecher
