#include "platform/mac/MacTargetProvider.h"

#include "platform/mac/MacCorrectionObserver.h"

#include <QEventLoop>
#include <QSet>
#include <QTimer>

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>

#include <limits>

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

bool isFocusedElement(AXUIElementRef expected)
{
    if (!expected) {
        return false;
    }
    AXUIElementRef focused = copyFocusedElement();
    const bool matches = focused && CFEqual(focused, expected);
    if (focused) {
        CFRelease(focused);
    }
    return matches;
}

std::optional<CFRange> rangeAttribute(AXUIElementRef element, CFStringRef attribute)
{
    if (!element) {
        return std::nullopt;
    }
    CFTypeRef value = nullptr;
    if (AXUIElementCopyAttributeValue(element, attribute, &value) != kAXErrorSuccess || !value) {
        return std::nullopt;
    }
    CFRange range;
    const bool valid = CFGetTypeID(value) == AXValueGetTypeID()
        && AXValueGetType(static_cast<AXValueRef>(value)) == kAXValueTypeCFRange
        && AXValueGetValue(static_cast<AXValueRef>(value), kAXValueTypeCFRange, &range);
    CFRelease(value);
    return valid ? std::optional<CFRange>(range) : std::nullopt;
}

std::optional<int> selectedTextOffset(AXUIElementRef element)
{
    const std::optional<CFRange> range = rangeAttribute(element, kAXSelectedTextRangeAttribute);
    if (!range || range->location < 0
        || range->location > std::numeric_limits<int>::max()) {
        return std::nullopt;
    }
    return static_cast<int>(range->location);
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

// Most Cocoa controls apply an AX write synchronously, but some only publish
// the new value on their next run-loop turn.
constexpr int insertionVerificationAttempts = 5;
constexpr int insertionVerificationPauseMs = 30;
constexpr int targetContextCharacters = 240;

// Sleeping here would freeze Speecher's own event loop, including the dictation
// popup. Spinning it instead keeps the UI alive; user input stays excluded so a
// stray keystroke cannot reach Speecher while the target still has focus.
void spinEventLoop(int milliseconds)
{
    QEventLoop wait;
    QTimer::singleShot(milliseconds, &wait, &QEventLoop::quit);
    wait.exec(QEventLoop::ExcludeUserInputEvents);
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
    // A new capture means the previous insertion is history, so any observation
    // of it stops here, exactly as clearing the AT-SPI snapshot does on Linux.
    if (m_correctionObserver) {
        m_correctionObserver->cancel();
    }
    m_valueBeforeInsertion.reset();
    m_insertionOffset.reset();
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
        m_focusedElement = const_cast<void *>(static_cast<const void *>(focused));
        target.accessible = true;
        target.role = stringAttribute(focused, kAXRoleAttribute);
        target.controlName = stringAttribute(focused, kAXTitleAttribute);
        if (!target.secure) {
            const std::optional<CFRange> selectedRange = rangeAttribute(
                focused, kAXSelectedTextRangeAttribute);
            if (selectedRange && selectedRange->location >= 0 && selectedRange->length >= 0
                && selectedRange->location <= std::numeric_limits<int>::max()
                && selectedRange->length
                    <= std::numeric_limits<int>::max() - selectedRange->location) {
                const int start = static_cast<int>(selectedRange->location);
                const int length = static_cast<int>(selectedRange->length);
                target.caretOffset = start;
                if (length > 0) {
                    target.selectionStart = start;
                    target.selectionEnd = start + length;
                    target.selectedText = stringAttribute(focused, kAXSelectedTextAttribute);
                }

                const QString value = stringAttribute(focused, kAXValueAttribute);
                if (start <= value.size() && length <= value.size() - start) {
                    target.nearbyTextBefore = value.left(start).right(targetContextCharacters);
                    target.nearbyTextAfter = value.mid(start, targetContextCharacters);
                }
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
        && isFocusedElement(static_cast<AXUIElementRef>(m_focusedElement))
        && selectedTextOffset(static_cast<AXUIElementRef>(m_focusedElement)).has_value()
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

    // Captured before the write so verifyInsertion can tell a real insertion
    // from a control that already happened to contain the text.
    m_valueBeforeInsertion = stringAttribute(static_cast<AXUIElementRef>(m_focusedElement),
                                             kAXValueAttribute);
    m_insertionOffset = selectedTextOffset(static_cast<AXUIElementRef>(m_focusedElement));
    if (!m_insertionOffset) {
        if (error) {
            *error = QStringLiteral("The focused control did not report its insertion point");
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
    if (!m_focusedElement || plainText.isEmpty() || target.secure || !stillFocused(target)
        || !isFocusedElement(static_cast<AXUIElementRef>(m_focusedElement))) {
        return false;
    }
    for (int attempt = 0; attempt < insertionVerificationAttempts; ++attempt) {
        if (attempt > 0) {
            spinEventLoop(insertionVerificationPauseMs);
        }
        const QString value = stringAttribute(static_cast<AXUIElementRef>(m_focusedElement),
                                              kAXValueAttribute);
        const bool changed = !m_valueBeforeInsertion || value != *m_valueBeforeInsertion;
        if (!changed || !m_insertionOffset
            || value.mid(*m_insertionOffset, plainText.size()) != plainText) {
            continue;
        }
        observeCorrections(target, value, *m_insertionOffset, plainText);
        return true;
    }
    return false;
}

void MacTargetProvider::observeCorrections(const Target &target,
                                           const QString &value,
                                           int insertedAt,
                                           const QString &plainText)
{
    const QString prefix = value.left(insertedAt).right(correctionContextChars);
    const QString suffix = value.mid(insertedAt + plainText.size()).left(correctionContextChars);
    // With too little context on either side the span cannot be found again once
    // the user has edited it, and learning from the wrong span is worse than not
    // learning at all.
    if (!m_correctionObservationEnabled
        || prefix.size() < correctionMinContextChars
        || suffix.size() < correctionMinContextChars) {
        return;
    }
    if (!m_correctionObserver) {
        m_correctionObserver = std::make_unique<mac::CorrectionObserver>();
    }
    m_correctionObserver->observe(
        m_focusedElement, target.processId, {target, plainText, prefix, suffix},
        [this](const QString &original, const QString &corrected,
               const QString &applicationId, double confidence) {
            emit correctionObserved(original, corrected, applicationId, confidence);
        });
}

void MacTargetProvider::setCorrectionObservationEnabled(bool enabled)
{
    m_correctionObservationEnabled = enabled;
    if (m_correctionObserver) {
        m_correctionObserver->setEnabled(enabled);
    }
}

} // namespace speecher
