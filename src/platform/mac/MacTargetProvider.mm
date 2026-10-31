#include "platform/mac/MacTargetProvider.h"

#include "platform/mac/MacCorrectionObserver.h"

#include <QPointer>
#include <QSet>

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>

#include <limits>
#include <optional>
#include <utility>

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

std::optional<QString> readStringAttribute(AXUIElementRef element, CFStringRef attribute)
{
    if (!element) {
        return {};
    }
    CFTypeRef value = nullptr;
    if (AXUIElementCopyAttributeValue(element, attribute, &value) != kAXErrorSuccess || !value) {
        return {};
    }
    std::optional<QString> text;
    if (CFGetTypeID(value) == CFStringGetTypeID()) {
        text = QString::fromCFString(static_cast<CFStringRef>(value));
    }
    CFRelease(value);
    return text;
}

QString stringAttribute(AXUIElementRef element, CFStringRef attribute)
{
    return readStringAttribute(element, attribute).value_or(QString{});
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

// The selection the write will replace, as [start, end) offsets.
std::optional<std::pair<int, int>> selectedTextSpan(AXUIElementRef element)
{
    const std::optional<CFRange> range = rangeAttribute(element, kAXSelectedTextRangeAttribute);
    if (!range || range->location < 0 || range->length < 0
        || range->location > std::numeric_limits<int>::max()
        || range->length > std::numeric_limits<int>::max() - range->location) {
        return std::nullopt;
    }
    return std::pair<int, int>(static_cast<int>(range->location),
                               static_cast<int>(range->location + range->length));
}

// The control's value and the selection a write will replace, or nothing when
// the value can't be read.
std::optional<InsertionBaseline> baselineOf(AXUIElementRef element, std::pair<int, int> span)
{
    const std::optional<QString> value = readStringAttribute(element, kAXValueAttribute);
    if (!value) {
        return std::nullopt;
    }
    return InsertionBaseline::forSelection(*value, span.first, span.second);
}

// Window numbers remain available without Accessibility permission, but their
// stacking order cannot identify keyboard focus when the app owns several.
CGWindowID singleOwnedWindow(pid_t processId)
{
    CFArrayRef windows = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
    if (!windows) return kCGNullWindowID;
    CGWindowID result = kCGNullWindowID;
    for (NSDictionary *window in (__bridge NSArray *)windows) {
        if ([window[(__bridge NSString *)kCGWindowOwnerPID] intValue] == processId) {
            if (result != kCGNullWindowID) {
                result = kCGNullWindowID;
                break;
            }
            result = [window[(__bridge NSString *)kCGWindowNumber] unsignedIntValue];
        }
    }
    CFRelease(windows);
    return result;
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

// Whether the attribute can be set; nothing when the element can't say.
std::optional<bool> attributeSettable(AXUIElementRef element, CFStringRef attribute)
{
    Boolean settable = false;
    if (!element || AXUIElementIsAttributeSettable(element, attribute, &settable) != kAXErrorSuccess) {
        return std::nullopt;
    }
    return settable;
}

bool selectedTextIsSettable(AXUIElementRef element)
{
    return attributeSettable(element, kAXSelectedTextAttribute).value_or(false);
}

// Whether the control takes typing. Its value says so wherever it can be
// asked: WebKit lets the selected text of any text control be set, read-only
// or not, and Qt's editors let only their value be set. Where the value
// can't be asked about, the selected text decides.
bool takesTyping(AXUIElementRef element)
{
    if (const std::optional<bool> value = attributeSettable(element, kAXValueAttribute)) {
        return *value;
    }
    return selectedTextIsSettable(element);
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
    m_windowId = 0;
    m_insertionBaseline.reset();
    if (m_focusedElement) {
        CFRelease(static_cast<AXUIElementRef>(m_focusedElement));
        m_focusedElement = nullptr;
    }
}

void MacTargetProvider::capture(QObject *context, std::function<void(const Target &)> done)
{
    Q_UNUSED(context);
    done(captureNow());
}

Target MacTargetProvider::captureNow()
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
    m_windowId = singleOwnedWindow(frontmost.processIdentifier);
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
        // A web page or other read-only text reports its selection but takes
        // no replacement for it.
        target.editable = takesTyping(focused);
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
                    target.setNearbyText(value, start, start + length);
                }
            }
        }
    }

    target.terminalHost = terminalBundleIdentifiers().contains(target.applicationId);
    // A terminal shows output, and a paste there goes in at the prompt
    // rather than over the selection, so its text is never edited.
    if (target.terminalHost) {
        target.editable = false;
    }
    return target;
}

bool MacTargetProvider::stillFocused(const Target &target)
{
    if (target.processId <= 0) {
        return false;
    }
    NSRunningApplication *frontmost = [NSWorkspace sharedWorkspace].frontmostApplication;
    if (!frontmost || frontmost.processIdentifier != target.processId) return false;
    // AX identifies editable panels and popovers as well as normal windows.
    if (m_focusedElement) return isFocusedElement(static_cast<AXUIElementRef>(m_focusedElement));
    return m_windowId != kCGNullWindowID && singleOwnedWindow(target.processId) == m_windowId;
}

bool MacTargetProvider::canInsertText(const Target &target)
{
    const auto element = static_cast<AXUIElementRef>(m_focusedElement);
    // WebKit lets the selected text of a read-only control be set too, so
    // that alone does not make the control take the insertion.
    return !target.secure
        && stillFocused(target)
        && selectedTextOffset(element).has_value()
        && selectedTextIsSettable(element)
        && takesTyping(element);
}

bool MacTargetProvider::insertText(const Target &target, const QString &plainText, QString *error)
{
    if (!canInsertText(target)) {
        if (error) {
            *error = QStringLiteral("The focused control does not accept direct text insertion");
        }
        return false;
    }

    const auto element = static_cast<AXUIElementRef>(m_focusedElement);
    const auto span = selectedTextSpan(element);
    if (!span) {
        if (error) {
            *error = QStringLiteral("The focused control did not report its insertion point");
        }
        return false;
    }
    // Captured before the write so verifyInsertion can tell a real insertion
    // from a control that already happened to contain the text.
    m_insertionBaseline = baselineOf(element, *span);

    // Setting the selected text replaces the selection, or inserts at the caret
    // when there is none.
    CFStringRef value = plainText.toCFString();
    const AXError result = AXUIElementSetAttributeValue(element, kAXSelectedTextAttribute, value);
    CFRelease(value);
    if (result != kAXErrorSuccess) {
        if (error) {
            *error = QStringLiteral("The focused control rejected the inserted text");
        }
        return false;
    }
    return true;
}

bool MacTargetProvider::preparePaste(const Target &target)
{
    m_insertionBaseline.reset();
    if (target.secure || IsSecureEventInputEnabled()) return false;
    // Shared delivery permits a global paste rule without an identified target.
    // Keep that fallback, but never claim verification without a baseline.
    if (!target.hasIdentity()) return true;
    if (!stillFocused(target)) return false;
    const auto element = static_cast<AXUIElementRef>(m_focusedElement);
    if (const auto span = selectedTextSpan(element)) {
        m_insertionBaseline = baselineOf(element, *span);
    }
    return true;
}

void MacTargetProvider::verifyInsertion(const Target &target,
                                        const QString &plainText,
                                        QObject *context,
                                        std::function<void(bool)> done)
{
    if (!m_focusedElement || !m_insertionBaseline || target.secure || !stillFocused(target)) {
        done(false);
        return;
    }
    // A copy, because a capture before the answer resets the member.
    const InsertionBaseline baseline = *m_insertionBaseline;
    const auto read = [this]() -> std::optional<QString> {
        if (!m_focusedElement) {
            return std::nullopt;
        }
        return stringAttribute(static_cast<AXUIElementRef>(m_focusedElement), kAXValueAttribute);
    };
    baseline.verify(plainText, read, this,
                    [this, baseline, target, plainText, context = QPointer<QObject>(context), done](bool shown) {
                        if (!context) {
                            return;
                        }
                        if (shown) {
                            if (const auto window = baseline.correctionWindow(target, plainText)) {
                                observeCorrections(*window);
                            }
                        }
                        done(shown);
                    });
}

void MacTargetProvider::observeCorrections(const CorrectionWindow &window)
{
    if (!m_correctionObservationEnabled) {
        return;
    }
    if (!m_correctionObserver) {
        m_correctionObserver = std::make_unique<mac::CorrectionObserver>();
    }
    m_correctionObserver->observe(
        m_focusedElement, window.target.processId, window,
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
