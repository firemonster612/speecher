#include "platform/mac/MacTargetProvider.h"

#include <QSet>
#include <QThread>

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>

namespace speecher {
namespace {

// The built-in recognition rules name Linux terminals; on macOS the bundle
// identifier is what tells a terminal apart, and paste rules key off that.
const QSet<QString> &terminalBundleIdentifiers()
{
    static const QSet<QString> identifiers{
        QStringLiteral("com.apple.Terminal"),
        QStringLiteral("com.googlecode.iterm2"),
        QStringLiteral("dev.warp.Warp"),
        QStringLiteral("com.github.wez.wezterm"),
        QStringLiteral("net.kovidgoyal.kitty"),
        QStringLiteral("com.mitchellh.ghostty"),
        QStringLiteral("org.alacritty"),
    };
    return identifiers;
}

QString stringAttribute(AXUIElementRef element, CFStringRef attribute)
{
    if (!element) {
        return {};
    }
    CFTypeRef value = nullptr;
    if (AXUIElementCopyAttributeValue(element, attribute, &value) != kAXErrorSuccess || !value) {
        return {};
    }
    QString text;
    if (CFGetTypeID(value) == CFStringGetTypeID()) {
        text = QString::fromCFString(static_cast<CFStringRef>(value));
    }
    CFRelease(value);
    return text;
}

AXUIElementRef copyElementAttribute(AXUIElementRef element, CFStringRef attribute)
{
    if (!element) {
        return nullptr;
    }
    CFTypeRef value = nullptr;
    if (AXUIElementCopyAttributeValue(element, attribute, &value) != kAXErrorSuccess || !value) {
        return nullptr;
    }
    if (CFGetTypeID(value) != AXUIElementGetTypeID()) {
        CFRelease(value);
        return nullptr;
    }
    return static_cast<AXUIElementRef>(const_cast<void *>(value));
}

AXUIElementRef copyFocusedElement()
{
    if (!AXIsProcessTrusted()) {
        return nullptr;
    }
    AXUIElementRef systemWide = AXUIElementCreateSystemWide();
    if (!systemWide) {
        return nullptr;
    }
    AXUIElementRef focused = copyElementAttribute(systemWide, kAXFocusedUIElementAttribute);
    CFRelease(systemWide);
    return focused;
}

QString focusedWindowTitle(pid_t processId)
{
    if (!AXIsProcessTrusted()) {
        return {};
    }
    AXUIElementRef application = AXUIElementCreateApplication(processId);
    if (!application) {
        return {};
    }
    AXUIElementRef window = copyElementAttribute(application, kAXFocusedWindowAttribute);
    const QString title = stringAttribute(window, kAXTitleAttribute);
    if (window) {
        CFRelease(window);
    }
    CFRelease(application);
    return title;
}

bool selectedTextIsSettable(AXUIElementRef element)
{
    if (!element) {
        return false;
    }
    Boolean settable = false;
    return AXUIElementIsAttributeSettable(element, kAXSelectedTextAttribute, &settable) == kAXErrorSuccess
        && settable;
}

} // namespace

MacTargetProvider::MacTargetProvider(QObject *parent)
    : TargetProvider(parent)
{
}

MacTargetProvider::~MacTargetProvider()
{
    releaseFocusedElement();
}

void MacTargetProvider::releaseFocusedElement()
{
    if (m_focusedElement) {
        CFRelease(static_cast<AXUIElementRef>(m_focusedElement));
        m_focusedElement = nullptr;
    }
}

Target MacTargetProvider::capture(const QList<AppRecognitionRule> &recognitionRules)
{
    releaseFocusedElement();

    Target target;
    NSRunningApplication *frontmost = [NSWorkspace sharedWorkspace].frontmostApplication;
    if (!frontmost) {
        return target;
    }

    if (NSString *bundleIdentifier = frontmost.bundleIdentifier) {
        target.applicationId = QString::fromNSString(bundleIdentifier);
    }
    if (NSString *name = frontmost.localizedName) {
        target.applicationName = QString::fromNSString(name);
    }
    if (NSString *executable = frontmost.executableURL.lastPathComponent) {
        target.processName = QString::fromNSString(executable);
    }
    target.processId = frontmost.processIdentifier;
    target.windowTitle = focusedWindowTitle(frontmost.processIdentifier);
    // A password field grabs secure event input, which also blocks the CGEvent
    // paste, so delivery has to fall back to the clipboard.
    target.secure = IsSecureEventInputEnabled();

    AXUIElementRef focused = copyFocusedElement();
    if (focused) {
        m_focusedElement = focused;
        target.accessible = true;
        target.role = stringAttribute(focused, kAXRoleAttribute);
        target.controlName = stringAttribute(focused, kAXTitleAttribute);
        if (!target.secure) {
            target.selectedText = stringAttribute(focused, kAXSelectedTextAttribute);
            if (!target.selectedText.isEmpty()) {
                target.selectionStart = 0;
                target.selectionEnd = target.selectedText.size();
            }
        }
    }

    target.terminalHost = terminalBundleIdentifiers().contains(target.applicationId);
    target.category = classifyTarget(target, recognitionRules);
    if (target.terminalHost && target.category == AppCategory::General) {
        target.category = AppCategory::Terminal;
    }
    return target;
}

bool MacTargetProvider::stillFocused(const Target &target)
{
    if (target.processId <= 0) {
        return false;
    }
    NSRunningApplication *frontmost = [NSWorkspace sharedWorkspace].frontmostApplication;
    return frontmost && frontmost.processIdentifier == target.processId;
}

bool MacTargetProvider::canInsertText(const Target &target)
{
    return !target.secure
        && stillFocused(target)
        && selectedTextIsSettable(static_cast<AXUIElementRef>(m_focusedElement));
}

bool MacTargetProvider::insertText(const Target &target, const QString &plainText, QString *error)
{
    if (!canInsertText(target)) {
        if (error) {
            *error = QStringLiteral("The focused control does not accept direct text insertion");
        }
        return false;
    }

    // Setting the selected text replaces the selection, or inserts at the caret
    // when there is none.
    CFStringRef value = plainText.toCFString();
    const AXError result = AXUIElementSetAttributeValue(
        static_cast<AXUIElementRef>(m_focusedElement), kAXSelectedTextAttribute, value);
    CFRelease(value);
    if (result != kAXErrorSuccess) {
        if (error) {
            *error = QStringLiteral("The focused control rejected the inserted text");
        }
        return false;
    }
    return true;
}

bool MacTargetProvider::verifyInsertion(const Target &target, const QString &plainText)
{
    if (!m_focusedElement || plainText.isEmpty() || target.secure || !stillFocused(target)) {
        return false;
    }
    // Most Cocoa controls apply an AX write synchronously, but some only publish
    // the new value on their next run-loop turn.
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (attempt > 0) {
            QThread::msleep(30);
        }
        const QString value = stringAttribute(static_cast<AXUIElementRef>(m_focusedElement),
                                              kAXValueAttribute);
        if (value.contains(plainText)) {
            return true;
        }
    }
    return false;
}

void MacTargetProvider::setCorrectionObservationEnabled(bool enabled)
{
    // No-op: correction learning needs the AX text-change notifications the
    // Linux provider gets from AT-SPI. Nothing observes them on macOS yet.
    Q_UNUSED(enabled)
}

} // namespace speecher
