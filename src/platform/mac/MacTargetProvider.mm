#include "platform/mac/MacTargetProvider.h"

#include "platform/mac/MacCorrectionObserver.h"

#include <QTimer>

#include <array>

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>

#include <limits>
#include <optional>
#include <utility>

namespace speecher {
namespace {

// Chromium and Electron build their accessibility tree only once an assistive
// app asks for it, and build it in the background: when, after asking, a
// capture looks at the app again. Electron apps such as VS Code show their
// text within about 50 ms; Chrome takes about 2 s.
constexpr std::array<int, 3> accessibilityTreeLooksMs = {250, 1000, 2500};
// Electron's request for the tree, and Chromium's.
const CFStringRef manualAccessibilityAttribute = CFSTR("AXManualAccessibility");
const CFStringRef enhancedUserInterfaceAttribute = CFSTR("AXEnhancedUserInterface");

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

// The app's focused AX window, owned by the caller, or null.
AXUIElementRef copyFocusedWindow(pid_t processId)
{
    if (!AXIsProcessTrusted()) {
        return nullptr;
    }
    AXUIElementRef application = AXUIElementCreateApplication(processId);
    if (!application) {
        return nullptr;
    }
    AXUIElementRef window = copyElementAttribute(application, kAXFocusedWindowAttribute);
    CFRelease(application);
    return window;
}

// Whether the app's focused window is still window. A window the app could not
// name, then or now, counts as the same: Chrome answers for its windows before
// its tree is built, and a failed query mid-build is no window change.
bool stillInWindow(qint64 processId, const void *window)
{
    if (!window) {
        return true;
    }
    AXUIElementRef current = copyFocusedWindow(static_cast<pid_t>(processId));
    if (!current) {
        return true;
    }
    const bool same = CFEqual(current, static_cast<AXUIElementRef>(window));
    CFRelease(current);
    return same;
}

QString focusedWindowTitle(pid_t processId)
{
    AXUIElementRef window = copyFocusedWindow(processId);
    const QString title = stringAttribute(window, kAXTitleAttribute);
    if (window) {
        CFRelease(window);
    }
    return title;
}

// Whether the app's bundle holds Chromium, as Chrome, Edge, Brave and every
// Electron app do: a framework under Contents/Frameworks that is a known
// one, or another that carries Chromium's chrome_100_percent.pak.
bool bundlesChromium(pid_t processId)
{
    // Built once and kept: this file is not ARC, and setWithArray's set would
    // go with the first autorelease pool.
    static NSSet<NSString *> *const knownFrameworks = [[NSSet alloc] initWithArray:@[
        @"Electron Framework.framework", @"Chromium Framework.framework", @"Google Chrome Framework.framework",
        @"Microsoft Edge Framework.framework", @"Brave Browser Framework.framework",
    ]];
    NSURL *bundle = [NSRunningApplication runningApplicationWithProcessIdentifier:processId].bundleURL;
    NSURL *frameworks = [bundle URLByAppendingPathComponent:@"Contents/Frameworks" isDirectory:YES];
    NSFileManager *files = [NSFileManager defaultManager];
    for (NSString *name in [files contentsOfDirectoryAtPath:frameworks.path error:nil]) {
        if ([knownFrameworks containsObject:name]) {
            return true;
        }
        if ([name hasSuffix:@"Framework.framework"]
            && [files fileExistsAtPath:[frameworks.path stringByAppendingFormat:@"/%@/Resources/chrome_100_percent.pak",
                                                                               name]]) {
            return true;
        }
    }
    return false;
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

// Whether the app with processId is the one in front.
bool isFrontmost(qint64 processId)
{
    NSRunningApplication *frontmost = [NSWorkspace sharedWorkspace].frontmostApplication;
    return processId > 0 && frontmost && frontmost.processIdentifier == processId;
}

} // namespace

MacTargetProvider::MacTargetProvider(QObject *parent)
    : TargetProvider(parent)
{
}

MacTargetProvider::~MacTargetProvider()
{
    releaseFocusedElement();
    // AXEnhancedUserInterface slows window managers such as Rectangle and
    // Magnet, and the app's own animations, so it goes when Speecher does.
    for (const qint64 processId : std::as_const(m_enhancedInterfaceEnabled)) {
        if (AXUIElementRef application = AXUIElementCreateApplication(static_cast<pid_t>(processId))) {
            AXUIElementSetAttributeValue(application, enhancedUserInterfaceAttribute, kCFBooleanFalse);
            CFRelease(application);
        }
    }
}

// Asks a Chromium or Electron app, once, to build its accessibility tree, and
// says whether this call asked. Other apps are not asked, so a capture in
// them never waits. The tree stays on for the app's life.
bool MacTargetProvider::requestAccessibilityTree(qint64 processId)
{
    if (processId <= 0 || !AXIsProcessTrusted() || m_treeRequested.contains(processId)
        || !bundlesChromium(static_cast<pid_t>(processId))) {
        return false;
    }
    AXUIElementRef application = AXUIElementCreateApplication(static_cast<pid_t>(processId));
    if (!application) {
        return false;
    }
    m_treeRequested.insert(processId);
    AXUIElementSetAttributeValue(application, manualAccessibilityAttribute, kCFBooleanTrue);
    // VoiceOver may have turned it on already; then it is not Speecher's to
    // turn off.
    CFTypeRef enhanced = nullptr;
    AXUIElementCopyAttributeValue(application, enhancedUserInterfaceAttribute, &enhanced);
    const bool alreadyEnhanced = enhanced && CFEqual(enhanced, kCFBooleanTrue);
    if (enhanced) {
        CFRelease(enhanced);
    }
    // Chrome answers kAXErrorNotImplemented while the setting takes effect,
    // so whatever the answer, it is Speecher's to turn off.
    if (!alreadyEnhanced) {
        AXUIElementSetAttributeValue(application, enhancedUserInterfaceAttribute, kCFBooleanTrue);
        m_enhancedInterfaceEnabled.insert(processId);
    }
    CFRelease(application);
    return true;
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
    const Target target = captureNow();
    if (target.nearbyTextRead || !requestAccessibilityTree(target.processId)) {
        done(target);
        return;
    }
    const std::shared_ptr<const void> window(copyFocusedWindow(static_cast<pid_t>(target.processId)),
                                             [](const void *window) {
                                                 if (window) CFRelease(static_cast<AXUIElementRef>(window));
                                             });
    lookAgain(context, target, 0, window, std::move(done));
}

// Answers with the first look that reads the control's text, or the last.
// Once another app or window comes to the front, it answers with what it had.
void MacTargetProvider::lookAgain(QPointer<QObject> context, const Target &target, std::size_t look,
                                  std::shared_ptr<const void> window, std::function<void(const Target &)> done)
{
    const quint64 capture = m_captures;
    const int wait = accessibilityTreeLooksMs[look] - (look > 0 ? accessibilityTreeLooksMs[look - 1] : 0);
    QTimer::singleShot(wait, this, [this, capture, target, context, look, window, done = std::move(done)] {
        if (!context) {
            return;
        }
        // A later capture tracks its own control, which another look here
        // would replace. The app and its window are compared, not the
        // control: until Chrome builds its tree there is no focused control
        // to compare, and the built tree may answer with a different element
        // for the same control. Another control in the same window within
        // the looks is taken as the same.
        if (m_captures != capture || !isFrontmost(target.processId)
            || !stillInWindow(target.processId, window.get())) {
            done(target);
            return;
        }
        const Target again = captureNow();
        if (again.nearbyTextRead || look + 1 == accessibilityTreeLooksMs.size()) {
            done(again);
            return;
        }
        lookAgain(context, again, look + 1, window, done);
    });
}

Target MacTargetProvider::captureNow()
{
    releaseFocusedElement();
    ++m_captures;

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

                // A value that can't be read leaves the nearby text unread,
                // not empty.
                const std::optional<QString> value = readStringAttribute(focused, kAXValueAttribute);
                if (value && start <= value->size() && length <= value->size() - start) {
                    target.setNearbyText(*value, start, start + length);
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
    if (!isFrontmost(target.processId)) return false;
    // AX identifies editable panels and popovers as well as normal windows.
    if (m_focusedElement) return isFocusedElement(static_cast<AXUIElementRef>(m_focusedElement));
    return m_windowId != kCGNullWindowID && singleOwnedWindow(target.processId) == m_windowId;
}

bool MacTargetProvider::canInsertText(const Target &target)
{
    const auto element = static_cast<AXUIElementRef>(m_focusedElement);
    // WebKit lets the selected text of a read-only control be set too, so
    // that alone does not make the control take the insertion. Chromium and
    // Electron answer the write with success while VS Code's editor and T3
    // Code's composer show nothing of it, so every app that bundles Chromium
    // gets the paste keys.
    return !target.secure
        && !bundlesChromium(static_cast<pid_t>(target.processId))
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
    const quint64 capture = m_captures;
    const auto read = [this, capture, target]() -> std::optional<QString> {
        // The answer comes in the same call as the read that passed, so this
        // also keeps a later capture's element from being observed.
        if (m_captures != capture || !m_focusedElement || !stillFocused(target)) {
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
