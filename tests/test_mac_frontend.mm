#include "common/test_local_setup.h"
#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "app/MacSparkleUpdater.h"
#include "core/AppSettings.h"
#include "core/SettingsStore.h"
#include "core/settings/SettingsKeys.h"
#include "core/settings/SettingsSchema.h"
#include "frontend/mac/MacFrontEnd.h"
#include "frontend/mac/SpeecherBridge.h"
#include "platform/mac/MacGlobalShortcutBinder.h"
#include "ui/AppWindow.h"
#include "ui/SetupAssistant.h"
#include "ui/TranscriberPopup.h"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>

// The Swift class's Objective-C runtime name is mangled, so a hand-written
// @interface cannot stand in for the generated header.
#import "SpeecherUI-Swift.h"

#include <QDeadlineTimer>
#include <QApplication>
#include <QFile>
#include <QDir>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <optional>

using namespace speecher;

namespace {

// Every widget, not only the top-level ones: giving one of these a parent must
// not be enough to make a count of them pass.
template<typename Widget>
int widgetCount()
{
    int count = 0;
    for (QWidget *widget : QApplication::allWidgets()) {
        count += dynamic_cast<Widget *>(widget) != nullptr;
    }
    return count;
}

SettingsRowModel *settingsRow(SettingsSchemaModel *schema, NSString *rowId)
{
    for (SettingsPageModel *page in schema.pages) {
        for (SettingsSectionModel *section in page.sections) {
            for (SettingsRowModel *row in section.rows) {
                if ([row.rowId isEqualToString:rowId]) {
                    return row;
                }
            }
        }
    }
    return nil;
}

// Whether the given key, with ⌃⌥⇧ unless told otherwise, is unregistered
// system-wide: Carbon's exclusive option refuses the registration while
// anyone — including this process's own shortcut binder — holds it.
bool hotKeyComboIsFree(UInt32 keyCode = kVK_F9, UInt32 modifiers = controlKey | optionKey | shiftKey)
{
    const EventHotKeyID identifier{'spct', 99};
    EventHotKeyRef probe = nullptr;
    const OSStatus status = RegisterEventHotKey(keyCode,
                                                modifiers,
                                                identifier,
                                                GetApplicationEventTarget(),
                                                kEventHotKeyExclusive,
                                                &probe);
    if (status == noErr && probe) {
        UnregisterEventHotKey(probe);
    }
    return status == noErr;
}

// An NSAccessibility attribute, which the protocol leaves optional; nil when
// the element has none.
id accessibilityAttribute(id element, SEL getter)
{
    return [element respondsToSelector:getter] ? [element valueForKey:NSStringFromSelector(getter)]
                                               : nil;
}

// What VoiceOver is given under element: role, subrole, label, title, value and
// frame of every node, one line each and indented by depth.
QString accessibilityTree(id element, int depth = 0)
{
    NSString *line = [NSString stringWithFormat:@"%*s%@ %@ label=%@ title=%@ value=%@ frame=%@\n",
        depth * 2, "",
        accessibilityAttribute(element, @selector(accessibilityRole)),
        accessibilityAttribute(element, @selector(accessibilitySubrole)),
        accessibilityAttribute(element, @selector(accessibilityLabel)),
        accessibilityAttribute(element, @selector(accessibilityTitle)),
        accessibilityAttribute(element, @selector(accessibilityValue)),
        accessibilityAttribute(element, @selector(accessibilityFrame))];
    QString tree = QString::fromNSString(line);
    for (id child in accessibilityAttribute(element, @selector(accessibilityChildren))) {
        tree += accessibilityTree(child, depth + 1);
    }
    return tree;
}

// An attribute as an accessibility client such as VoiceOver reads it.
id axAttribute(id element, CFStringRef attribute)
{
    CFTypeRef value = nullptr;
    AXUIElementCopyAttributeValue((__bridge AXUIElementRef)element, attribute, &value);
    return CFBridgingRelease(value);
}

// The first button under element, depth first, whose name as VoiceOver reads
// it starts with caption: a row button's name runs on into its description.
id axButton(id element, NSString *caption)
{
    NSString *description = axAttribute(element, kAXDescriptionAttribute);
    NSString *name = description.length > 0 ? description : axAttribute(element, kAXTitleAttribute);
    if ([axAttribute(element, kAXRoleAttribute) isEqual:(__bridge NSString *)kAXButtonRole]
        && [name hasPrefix:caption]) {
        return element;
    }
    for (id child in axAttribute(element, kAXChildrenAttribute)) {
        if (id found = axButton(child, caption)) {
            return found;
        }
    }
    return nil;
}

// The same, in this process's windows. SwiftUI builds its elements only for
// an accessibility client, so a test reaches its controls as one, which
// needs the Accessibility grant; the views' own NSAccessibility tree stays
// empty until a client has asked.
id axButtonOnScreen(NSString *caption)
{
    // A client lists no windows until the app has finished launching, which
    // the offscreen platform the suites run on, unlike Cocoa's, never does.
    static const bool launched = [] {
        if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
            [NSApp finishLaunching];
        }
        return true;
    }();
    Q_UNUSED(launched);
    id application = CFBridgingRelease(AXUIElementCreateApplication(getpid()));
    for (id window in axAttribute(application, kAXWindowsAttribute)) {
        if (id found = axButton(window, caption)) {
            return found;
        }
    }
    return nil;
}

bool axPress(id button)
{
    return AXUIElementPerformAction((__bridge AXUIElementRef)button, kAXPressAction) == kAXErrorSuccess;
}

// An autorelease pool for a scope that is not a block, through the runtime
// calls @autoreleasepool compiles to (clang's ARC specification).
extern "C" void *objc_autoreleasePoolPush(void);
extern "C" void objc_autoreleasePoolPop(void *pool);

struct AutoreleasePool {
    AutoreleasePool() : token(objc_autoreleasePoolPush()) {}
    ~AutoreleasePool() { objc_autoreleasePoolPop(token); }
    AutoreleasePool(const AutoreleasePool &) = delete;
    AutoreleasePool &operator=(const AutoreleasePool &) = delete;
    void *token;
};

// Lets SwiftUI lay out and AppKit draw what the last call changed.
void settle()
{
    const QDeadlineTimer deadline(300);
    while (!deadline.hasExpired()) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.02, true);
        QCoreApplication::processEvents();
    }
}

// Runs the event loop until object is released, as a closed window and the
// view it held are some turns after it closes, once accessibility, which a
// test may have woken, drops what it cached. False if it outlives that.
bool settleUntilReleased(__weak id &object)
{
    const QDeadlineTimer deadline(5000);
    while (object && !deadline.hasExpired()) {
        settle();
    }
    return !object;
}

// The visible dictation panel, which floats at the status bar's level.
NSWindow *dictationPanel()
{
    for (NSWindow *window in NSApp.windows) {
        if ([window isKindOfClass:[NSPanel class]] && window.visible && window.level == NSStatusWindowLevel) {
            return window;
        }
    }
    return nil;
}

// A test's mac UI over a bridge to its controller. Declared after the
// controller, it goes first, as the app's front end does: its windows close,
// the work they deferred runs while the controller can still answer it, and
// the UI and the bridge go, with every connection the bridge made.
struct NativeUi {
    explicit NativeUi(ApplicationController &controller)
        : bridge([[SpeecherBridge alloc] initWithController:&controller])
        , ui([[SpeecherMacUI alloc] initWithBridge:bridge])
    {
    }
    ~NativeUi()
    {
        [ui dismissDictationPanel];
        [ui hideSettings];
        [ui dismissSetupAssistant];
        __weak id released = bridge;
        ui = nil;
        bridge = nil;
        pool.reset();
        if (!settleUntilReleased(released)) {
            QTest::qFail("The mac UI outlived its test", __FILE__, __LINE__);
        }
    }
    // First, so it holds what making the UI autoreleases. A test runs under
    // the outermost pool, which never drains, and whatever AppKit and Swift
    // autorelease while the UI is up, the windows that hold it among them,
    // would otherwise keep it past the test.
    std::optional<AutoreleasePool> pool{std::in_place};
    SpeecherBridge *bridge;
    SpeecherMacUI *ui;
};

} // namespace

class MacFrontEndTests : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        SettingsStore settings;
        settings.raw().clear();
    }

    void retainedCollectionBaseline_data()
    {
        QTest::addColumn<bool>("scalarCommit");
        QTest::newRow("repeated collection saves") << false;
        QTest::newRow("scalar commit with retained editor") << true;
    }

    void retainedCollectionBaseline()
    {
        QFETCH(bool, scalarCommit);
        ApplicationController controller(false);
        SettingsStore *store = controller.settings();
        store->setLearnedCorrections({{"one", "githab", "GitHub", "editor", 100, 0.8, true, 1, 100}});
        SpeecherBridge *bridge = [[SpeecherBridge alloc] initWithController:&controller];
        SettingsSchemaModel *schema = bridge.settingsSchema;
        NSArray<SpeecherRecord *> *previous = settingsRow(schema, @"learnedCorrections").value;
        QCOMPARE(previous.count, NSUInteger(1));
        auto fresh = store->learnedCorrections();
        fresh[0].evidenceCount = 3;
        fresh.append({"two", "new", "newer", "editor", 300, 0.9, true, 1, 300});
        store->setLearnedCorrections(fresh);
        if (scalarCommit) {
            [schema setValue:@12 forRowId:@"previewWords"];
            [schema commit];
        }
        for (bool enabled : {false, true}) {
            NSMutableDictionary *record = [previous.firstObject mutableCopy];
            record[@"enabled"] = @(enabled);
            NSArray<SpeecherRecord *> *edited = @[record];
            NSArray<NSString *> *problems = [schema saveRecords:edited
                                               previousRecords:previous forRowId:@"learnedCorrections"];
            QCOMPARE(problems.count, NSUInteger(0));
            previous = edited;
            const auto saved = store->learnedCorrections();
            QCOMPARE(saved.size(), 2);
            QCOMPARE(saved[0].evidenceCount, 3);
            QCOMPARE(saved[0].enabled, enabled);
            QCOMPARE(saved[1].id, QStringLiteral("two"));
        }
        if (scalarCommit) QCOMPARE(store->previewWords(), 12);
    }

    void constructionDoesNotCreateAQtDictationPopup()
    {
        const int existingPopups = widgetCount<TranscriberPopup>();
        ApplicationController controller(false);
        // The front end's UI goes with the controller, not with the outermost
        // pool, which never drains (NativeUi).
        const AutoreleasePool pool;
        MacFrontEnd frontEnd(&controller);

        QCOMPARE(widgetCount<TranscriberPopup>(), existingPopups);
    }

    void nativeDictationProblemCanBeDismissed()
    {
        ApplicationController controller(false);
        NativeUi native(controller);
        SpeecherMacUI *ui = native.ui;

        [ui showDictationProblem:@"The microphone stopped" fix:nil];
        QVERIFY(ui.dictationPanelVisible);

        [ui dismissDictationPanel];
        QVERIFY(!ui.dictationPanelVisible);
    }

    void nativeDictationPanelUsesStatusWindowLevel()
    {
        ApplicationController controller(false);
        NativeUi native(controller);
        SpeecherMacUI *ui = native.ui;

        QCOMPARE(ui.dictationPanelLevel, NSInteger(NSStatusWindowLevel));
    }

    void popupPresentationAcknowledgesRequestedGeneration()
    {
        ApplicationController controller(false);
        NativeUi native(controller);
        SpeecherBridge *bridge = native.bridge;
        SpeecherMacUI *ui = native.ui;
        constexpr uint64_t generation = 73;

        QVERIFY(bridge.popupShowRequested);
        bridge.popupShowRequested(generation);

        // The acknowledgement is deferred through the GCD main queue, which
        // Qt's test event pump does not drain; only the CFRunLoop does.
        const QDeadlineTimer deadline(2000);
        while (ui.dictationPanelPresentedGeneration != generation && !deadline.hasExpired()) {
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, true);
            QCoreApplication::processEvents();
        }
        QCOMPARE(ui.dictationPanelPresentedGeneration, generation);
        [ui dismissDictationPanel];
    }

    void dictationPreviewGrowsUpwardAndRenewalUsesStandalonePill()
    {
        ApplicationController controller(false);
        NativeUi native(controller);
        SpeecherBridge *bridge = native.bridge;
        SpeecherMacUI *ui = native.ui;
        bridge.popupStatusChanged(@"Listening", SpeecherDictationStateListening);
        bridge.popupShowRequested(74);
        const auto settle = [] {
            const QDeadlineTimer deadline(300);
            while (!deadline.hasExpired()) {
                CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.02, true);
                QCoreApplication::processEvents();
            }
        };
        settle();
        NSWindow *panel = nil;
        for (NSWindow *window in NSApp.windows) {
            if ([window isKindOfClass:[NSPanel class]] && window.visible
                && window.level == NSStatusWindowLevel) {
                panel = window;
                break;
            }
        }
        QVERIFY(panel);
        // Each state is captured before its layout is checked, so a failing
        // check still leaves the pictures of it and every state before it.
        const QString directory = qEnvironmentVariable("SPEECHER_UPDATE_PREVIEW_DIR");
        const auto capture = [&](const QString &name) {
            if (directory.isEmpty()) return true;
            QDir().mkpath(directory);
            NSView *view = panel.contentView;
            NSBitmapImageRep *bitmap = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
            if (!bitmap) return false;
            [view cacheDisplayInRect:view.bounds toBitmapImageRep:bitmap];
            NSData *png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
            return bool([png writeToFile:(directory + "/mac-" + name + ".png").toNSString()
                              atomically:YES]);
        };
        QVERIFY(capture("listening"));
        const NSRect initial = panel.frame;
        QCOMPARE(initial.size.height, SpeecherPopupGeometry.pillHeight);
        // Paused keeps the pill, its bars flat in the caution colour.
        bridge.popupStatusChanged(@"Paused", SpeecherDictationStatePaused);
        settle();
        QVERIFY(capture("paused"));
        QCOMPARE(panel.frame.size.height, initial.size.height);
        bridge.popupStatusChanged(@"Listening", SpeecherDictationStateListening);
        bridge.popupPreviewChanged(@"short preview");
        settle();
        QVERIFY(capture("short-preview"));
        QVERIFY(panel.frame.size.height > initial.size.height);
        QCOMPARE(panel.frame.origin.y, initial.origin.y);
        // However few the words, the text bar stays wide enough to carve the
        // contour around the lobe of pause, dots and cancel, and nothing was
        // cut, so nothing fades.
        NSFont *font = [NSFont systemFontOfSize:NSFont.systemFontSize
                                                * SpeecherPopupGeometry.previewFontScale];
        const CGFloat dots = SpeecherPopupGeometry.barCount * SpeecherPopupGeometry.barWidth
                             + (SpeecherPopupGeometry.barCount - 1) * SpeecherPopupGeometry.barGap;
        const CGFloat lobe = 2 * (SpeecherPopupGeometry.lobeAir + SpeecherPopupGeometry.buttonSize
                                  + SpeecherPopupGeometry.buttonGap)
                             + dots;
        const CGFloat shoulder = SpeecherPopupGeometry.previewTopMargin
                                 + ceil(font.ascender - font.descender + font.leading)
                                 + SpeecherPopupGeometry.shoulderDrop;
        QVERIFY(panel.frame.size.width + 1
                >= [SpeecherPopupGeometry minimumPreviewBarWidthForLobeWidth:lobe shoulderHeight:shoulder]);
        QVERIFY(!ui.dictationPreviewFades);
        bridge.popupPreviewChanged(@"We should probably move the meeting to Thursday afternoon, after everyone has reviewed the latest draft.");
        settle();
        QVERIFY(capture("long-preview"));
        QVERIFY(panel.frame.size.width
                <= SpeecherPopupGeometry.maxPreviewWidth + 2 * SpeecherPopupGeometry.previewSideMargin);
        QCOMPARE(panel.frame.origin.y, initial.origin.y);
        // The oldest words were cut, so the line's start fades out.
        QVERIFY(ui.dictationPreviewFades);
        // Streaming text repeatedly changes the width; the palette must keep
        // its original center rather than accumulate rounding or layout drift.
        for (int update = 0; update < 20; ++update) {
            bridge.popupPreviewChanged(update % 2 == 0
                ? @"Please move the meeting to Thursday."
                : @"short preview");
            settle();
            QVERIFY(capture("streaming-preview"));
            QVERIFY2(qAbs(NSMidX(panel.frame) - NSMidX(initial)) <= 1,
                     qPrintable(QStringLiteral("Palette center moved from %1 to %2 after update %3")
                         .arg(NSMidX(initial)).arg(NSMidX(panel.frame)).arg(update)));
        }
        bridge.popupFrozenChanged(true);
        settle();
        QVERIFY(capture("frozen-preview"));
        bridge.popupStatusChanged(@"Stopping", SpeecherDictationStateStopping);
        settle();
        QVERIFY(capture("transcribing"));
        QCOMPARE(panel.frame.size.height, initial.size.height);
        // The label hugs its text, centred between the spinner in pause's
        // place and the cancel button.
        bridge.popupStatusChanged(@"Refining", SpeecherDictationStateRefining);
        bridge.popupRefiningChanged(true);
        settle();
        QVERIFY(capture("refining-no-text"));
        QCOMPARE(panel.frame.size.height, initial.size.height);
        qInfo().noquote() << "Refining panel's accessibility tree:\n"
                          << accessibilityTree(panel.contentView);
        // The frames are in the panel's coordinates, so its centre is half its width.
        const NSRect label = ui.dictationStatusFrame;
        const NSRect spinner = ui.dictationBusyFrame;
        const NSRect cancel = ui.dictationCancelFrame;
        QVERIFY2(!NSIsEmptyRect(label) && !NSIsEmptyRect(spinner) && !NSIsEmptyRect(cancel),
                 qPrintable(QStringLiteral("status %1, spinner %2, cancel %3")
                     .arg(QString::fromNSString(NSStringFromRect(label)),
                          QString::fromNSString(NSStringFromRect(spinner)),
                          QString::fromNSString(NSStringFromRect(cancel)))));
        QVERIFY(qAbs(NSMidX(label) - NSWidth(panel.frame) / 2) <= 1);
        QVERIFY(qAbs((NSMidX(label) - NSMidX(spinner)) - (NSMidX(cancel) - NSMidX(label))) <= 1);
        bridge.popupRefinementPreviewChanged(@"Move the meeting to Thursday afternoon.");
        settle();
        QVERIFY(capture("refining"));
        QVERIFY(panel.frame.size.height > initial.size.height);
        QCOMPARE(panel.frame.origin.y, initial.origin.y);
        QVERIFY(qAbs(NSMidX(panel.frame) - NSMidX(initial)) <= 1);
        bridge.popupFrozenChanged(false);
        bridge.popupOAuthRefreshRequested();
        settle();
        QVERIFY(capture("renewal"));
        QCOMPARE(panel.frame.size.height, initial.size.height);
        QVERIFY(panel.frame.size.width < 200);
        bridge.popupListeningIndicatorRequested();
        settle();
        QCOMPARE(panel.frame.size.width, initial.size.width);

        // Receipts carry their outcome's symbol; a problem wraps at the shared
        // width and grows taller, with its countdown beneath.
        SpeecherErrorAction *noFix = [[SpeecherErrorAction alloc] initWithFix:SpeecherErrorFixNone pageId:@""];
        QVERIFY(bridge.popupMessageRequested);
        bridge.popupMessageRequested(@"Input sent", SpeecherPopupOutcomeInserted, noFix);
        settle();
        QVERIFY(capture("receipt-inserted"));
        // A receipt shares the waveform's pill.
        QCOMPARE(panel.frame.size.height, SpeecherPopupGeometry.pillHeight);
        bridge.popupMessageRequested(@"Copied", SpeecherPopupOutcomeCopied, noFix);
        settle();
        QVERIFY(capture("receipt-copied"));
        bridge.popupErrorRequested(@"Microphone unavailable", noFix);
        settle();
        const CGFloat shortError = panel.frame.size.height;
        QVERIFY(capture("error-short"));
        bridge.popupErrorRequested(@"The transcription service rejected the request: the API key "
                                   @"is invalid or has expired. Check the key on the Accounts "
                                   @"page, then try again.",
                                   noFix);
        settle();
        QVERIFY(capture("error-long"));
        QVERIFY(panel.frame.size.height > shortError);
        QVERIFY(panel.frame.size.width <= SpeecherBridge.popupErrorWrapWidth + 200);
        bridge.popupErrorRequested(@"Could not reach https://example.com/"
                                   @"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
                                   noFix);
        settle();
        QVERIFY(capture("error-unbroken"));
        QVERIFY(panel.frame.size.width <= SpeecherBridge.popupErrorWrapWidth + 200);
    }

    // A fixable outcome carries its fix as a button, which opens the page the
    // error path would; an outcome with none looks as it always has.
    void outcomeOffersItsFixOnlyWithOne()
    {
        ApplicationController controller(false);
        NativeUi native(controller);
        SpeecherBridge *bridge = native.bridge;
        SpeecherMacUI *ui = native.ui;
        bridge.popupStatusChanged(@"Listening", SpeecherDictationStateListening);
        bridge.popupShowRequested(75);
        settle();
        NSWindow *panel = dictationPanel();
        QVERIFY(panel);
        const auto capture = [&](const QString &name) {
            const QString directory = qEnvironmentVariable("SPEECHER_UPDATE_PREVIEW_DIR");
            if (directory.isEmpty()) return;
            QDir().mkpath(directory);
            NSView *view = panel.contentView;
            NSBitmapImageRep *bitmap = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
            [view cacheDisplayInRect:view.bounds toBitmapImageRep:bitmap];
            [[bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}]
                writeToFile:(directory + "/mac-" + name + ".png").toNSString() atomically:YES];
        };

        SpeecherErrorAction *accounts = [[SpeecherErrorAction alloc] initWithFix:SpeecherErrorFixSettingsPage
                                                                          pageId:@"accounts"];
        QVERIFY(accounts.label.length > 0);
        const auto showFixable = [&] {
            bridge.popupMessageRequested(@"Input sent • Used Local Model. Your ChatGPT sign-in has expired.",
                                         SpeecherPopupOutcomeFallback, accounts);
            settle();
        };
        showFixable();
        capture("outcome-fix");
        const NSRect fix = ui.dictationOutcomeFixFrame;
        QVERIFY2(!NSIsEmptyRect(fix), qPrintable(QString::fromNSString(NSStringFromRect(fix))));
        QVERIFY(panel.frame.size.width <= SpeecherBridge.popupErrorWrapWidth + 300);

        SpeecherErrorAction *noFix = [[SpeecherErrorAction alloc] initWithFix:SpeecherErrorFixNone pageId:@""];
        bridge.popupMessageRequested(@"Input sent", SpeecherPopupOutcomeInserted, noFix);
        settle();
        capture("outcome-no-fix");
        QVERIFY(NSIsEmptyRect(ui.dictationOutcomeFixFrame));
        QCOMPARE(panel.frame.size.height, SpeecherPopupGeometry.pillHeight);

        showFixable();
        QVERIFY2(AXIsProcessTrusted(), "pressing the fix as VoiceOver does needs the Accessibility grant");
        id button = axButtonOnScreen(accounts.label);
        QVERIFY(button);
        QVERIFY(axPress(button));
        settle();
        QVERIFY(!ui.dictationPanelVisible);
        QVERIFY(ui.settingsWindowVisible);
        QCOMPARE(QString::fromNSString(ui.settingsPane), QStringLiteral("accounts"));
    }

    // The Fallbacks row opens its subpage with the parent still selected, its
    // buttons edit the list, and Back returns to the parent. A page id reaches
    // the subpage directly.
    void fallbacksSubpageWorksThroughItsControls()
    {
        ApplicationController controller(false);
        NativeUi native(controller);
        SpeecherMacUI *ui = native.ui;
        SettingsSchemaModel *schema = native.bridge.settingsSchema;
        [schema addFallback:SpeecherProviderRoleSpeech provider:@"endpoint"];
        [schema addFallback:SpeecherProviderRoleSpeech provider:@"codex"];
        [schema commit];

        SettingsRowModel *row = settingsRow(schema, @"speechFallbacks");
        QVERIFY(row);
        [ui openSettingsPage:@"dictation"];
        settle();
        QVERIFY2(AXIsProcessTrusted(), "pressing the controls as VoiceOver does needs the Accessibility grant");
        id opener = axButtonOnScreen(row.label);
        QVERIFY(opener);
        QVERIFY(axPress(opener));
        settle();
        QCOMPARE(QString::fromNSString(ui.settingsPane), QStringLiteral("dictation"));
        QCOMPARE(QString::fromNSString(ui.settingsSubpage), QStringLiteral("dictation:fallbacks"));

        // The first fallback's Move down; the second's is disabled.
        SpeecherFallbackList *list = settingsRow(schema, @"speechFallbackList").fallbackList;
        id moveDown = axButtonOnScreen(list.moveDownCaption);
        QVERIFY(moveDown);
        QVERIFY(axPress(moveDown));
        settle();
        QCOMPARE(controller.settings()->snapshot().speech.fallbackProviderIds,
                 (QStringList{QStringLiteral("codex"), QStringLiteral("endpoint")}));

        id back = axButtonOnScreen(SpeecherBridge.settingsBackCaption);
        QVERIFY(back);
        QVERIFY(axPress(back));
        settle();
        QCOMPARE(QString::fromNSString(ui.settingsPane), QStringLiteral("dictation"));
        QCOMPARE(QString::fromNSString(ui.settingsSubpage), QString());

        [ui openSettingsPage:@"refinement:fallbacks"];
        QCOMPARE(QString::fromNSString(ui.settingsPane), QStringLiteral("refinement"));
        QCOMPARE(QString::fromNSString(ui.settingsSubpage), QStringLiteral("refinement:fallbacks"));
        // Choosing another pane leaves the subpage.
        [ui openSettingsPage:@"general"];
        QCOMPARE(QString::fromNSString(ui.settingsSubpage), QString());
    }

    // The list's buttons edit through core's rules and save like any row, and
    // the list re-renders from what was saved.
    void fallbackListEditsPersist()
    {
        ApplicationController controller(false);
        SettingsStore *store = controller.settings();
        SpeecherBridge *bridge = [[SpeecherBridge alloc] initWithController:&controller];
        SettingsSchemaModel *schema = bridge.settingsSchema;
        const auto saved = [&] { return store->snapshot().speech.fallbackProviderIds; };

        SpeecherFallbackList *list = settingsRow(schema, @"speechFallbackList").fallbackList;
        QVERIFY(list);
        QVERIFY(list.role == SpeecherProviderRoleSpeech);
        QVERIFY(list.heading.length > 0);
        QCOMPARE(list.items.count, NSUInteger(0));
        QVERIFY(list.canAdd);

        [schema addFallback:SpeecherProviderRoleSpeech provider:@"endpoint"];
        [schema addFallback:SpeecherProviderRoleSpeech provider:@"codex"];
        [schema commit];
        QCOMPARE(saved(), (QStringList{QStringLiteral("endpoint"), QStringLiteral("codex")}));
        list = settingsRow(schema, @"speechFallbackList").fallbackList;
        QCOMPARE(list.items.count, NSUInteger(2));
        QVERIFY(!list.items[0].canMoveUp && list.items[0].canMoveDown);
        QVERIFY(list.items[1].canMoveUp && !list.items[1].canMoveDown);
        // Two fallbacks fill the chain.
        QVERIFY(!list.canAdd);

        [schema moveFallback:SpeecherProviderRoleSpeech at:0 by:1];
        [schema commit];
        QCOMPARE(saved(), (QStringList{QStringLiteral("codex"), QStringLiteral("endpoint")}));
        [schema removeFallback:SpeecherProviderRoleSpeech at:0];
        [schema commit];
        QCOMPARE(saved(), QStringList{QStringLiteral("endpoint")});
        QCOMPARE(QString::fromNSString(settingsRow(schema, @"speechFallbackList").fallbackList.items[0].providerId),
                 QStringLiteral("endpoint"));

        // A primary that is already a fallback leaves the list.
        [schema setValue:@"endpoint" forRowId:@"speechProvider"];
        [schema commit];
        QVERIFY(saved().isEmpty());
    }

    // The setup steps' section is optional: it shows with nothing in it, and
    // Skip cleanup hides it.
    void setupFallbackSectionIsOptional()
    {
        ApplicationController controller(false);
        SpeecherBridge *bridge = [[SpeecherBridge alloc] initWithController:&controller];

        SpeecherSetupFallbackSection *speech = [bridge setupFallbackSection:SpeecherProviderRoleSpeech];
        QVERIFY(speech.visible);
        QVERIFY(speech.hint.length > 0);
        QCOMPARE(speech.list.items.count, NSUInteger(0));
        QVERIFY(speech.list.canAdd);
        QVERIFY([bridge setupFallbackSection:SpeecherProviderRoleRefinement].visible);

        [bridge.settingsSchema setValue:@"none" forRowId:@"refinementProvider"];
        [bridge.settingsSchema commit];
        QVERIFY(![bridge setupFallbackSection:SpeecherProviderRoleRefinement].visible);
    }

    // Skip, all nine pages, and Finish are driven through the native AX tree
    // in macOS setup assistant E2E. This catches a Qt wizard returning here.
    void setupUsesANativeWindow()
    {
        const int existingQtAssistants = widgetCount<SetupAssistant>();
        const int existingQtWindows = widgetCount<AppWindow>();
        ApplicationController controller(false);
        // The front end's UI goes with the controller, not with the outermost
        // pool, which never drains (NativeUi).
        const AutoreleasePool pool;
        MacFrontEnd frontEnd(&controller);
        controller.setFrontEnd(&frontEnd);
        // Closing the assistant brings up the settings window.
        const auto teardown = qScopeGuard([&] {
            frontEnd.hideMainWindow();
            settle();
        });

        controller.showSetupAssistant();
        NSWindow *assistant = nil;
        for (NSWindow *window in NSApp.windows) {
            if (window.visible && [window.title isEqualToString:@"Speecher Setup Assistant"]) {
                assistant = window;
                break;
            }
        }
        QVERIFY(assistant);
        QCOMPARE(widgetCount<SetupAssistant>(), existingQtAssistants);
        QCOMPARE(widgetCount<AppWindow>(), existingQtWindows);
        QVERIFY(!controller.settings()->setupCompleted());
        [assistant close];
        QVERIFY(!controller.settings()->setupCompleted());
        // AppKit lets the closed window go in its own time, and SwiftUI may
        // still draw its view then, after the controller the view reads.
        assistant.contentViewController = nil;
    }

    // The assistant renders what the bridge's ProviderSignIn seams decide, so
    // driving the seams is driving the SwiftUI sign-in card's whole behavior.
    void setupSignInSeamsDriveTheCliProxyOptIn()
    {
        ApplicationController controller(false);
        controller.settings()->raw().clear();
        QTemporaryDir dir;
        controller.settings()->raw().setValue(QStringLiteral("cliproxy/oauthDir"), dir.path());
        SpeecherBridge *bridge = [[SpeecherBridge alloc] initWithController:&controller];

        QVERIFY(![bridge setupCliproxyAccountsAvailable]);
        QFile account(QDir(dir.path()).filePath(QStringLiteral("claude-a@example.com.json")));
        QVERIFY(account.open(QIODevice::WriteOnly));
        account.write(QByteArray("{\"type\":\"claude\",\"access_token\":\"token\","
                                 "\"refresh_token\":\"refresh\",\"expired\":\"2099-01-01T00:00:00Z\"}"));
        account.close();
        QVERIFY([bridge setupCliproxyAccountsAvailable]);

        QVERIFY([bridge setupSupportsCliproxyForProvider:@"claude"]);
        QVERIFY(![bridge setupUsesCliproxyForProvider:@"claude"]);
        [bridge setSetupUseCliproxy:YES forProvider:@"claude"];
        QVERIFY([bridge setupUsesCliproxyForProvider:@"claude"]);
        QCOMPARE(controller.settings()->anthropicAuthMode(), QStringLiteral("cliproxy"));

        NSArray<RowOptionModel *> *options =
            [bridge setupCliproxyAccountOptionsForProvider:@"claude"];
        QCOMPARE(int(options.count), 1);
        [bridge setSetupCliproxyAccount:options.firstObject.rowOptionId forProvider:@"claude"];
        QCOMPARE(controller.settings()->anthropicCliproxyAccount(),
                 QStringLiteral("claude-a@example.com.json"));

        // Opting out restores the sign-in the bridge first saw.
        [bridge setSetupUseCliproxy:NO forProvider:@"claude"];
        QCOMPARE(controller.settings()->anthropicAuthMode(), QStringLiteral("oauth"));

        QCOMPARE(QString::fromNSString([bridge setupCliproxyDirectory]), dir.path());
        [bridge setSetupCliproxyDirectory:@"/custom/cliproxy"];
        QCOMPARE(controller.settings()->configuredCliproxyOauthDir(),
                 QStringLiteral("/custom/cliproxy"));
    }

    void settingsCapabilitiesFollowAccessibilityChanges()
    {
        ApplicationController controller(false);
        SpeecherBridge *bridge = [[SpeecherBridge alloc] initWithController:&controller];
        SettingsRowModel *row = settingsRow(bridge.settingsSchema, @"targetContextControl");
        QVERIFY(row);
        QVERIFY(!row.enabled);

        controller.accessibilityStateChanged(true, true, true);

        SettingsRowModel *refreshed = settingsRow(bridge.settingsSchema, @"targetContextControl");
        QVERIFY(refreshed);
        QVERIFY(refreshed.enabled);
    }

    // Paste with picks how to paste; inserting directly is a Default paste choice.
    void defaultPasteOffersAccessibilityInsertion()
    {
        ApplicationController controller(false);
        SpeecherBridge *bridge = [[SpeecherBridge alloc] initWithController:&controller];
        const auto offersInsertion = [](SettingsRowModel *row) {
            bool found = false;
            for (RowOptionModel *option in row.options) {
                found = found || [option.rowOptionId isEqualToString:@"direct_insert"];
            }
            return found;
        };
        SettingsRowModel *method = settingsRow(bridge.settingsSchema, @"outputMethod");
        SettingsRowModel *defaultPaste = settingsRow(bridge.settingsSchema, @"globalPasteRule");
        QVERIFY(method && defaultPaste);
        QVERIFY(!offersInsertion(method));
        QVERIFY(offersInsertion(defaultPaste));
    }

    void automaticDownloadsAppearForSparkle()
    {
        ApplicationController controller(false);
        SpeecherBridge *bridge = [[SpeecherBridge alloc] initWithController:&controller];
        SettingsRowModel *row = settingsRow(bridge.settingsSchema, @"autoInstallUpdates");

        QVERIFY(row);
        QVERIFY([row.help containsString:@"Sparkle"]);
    }

    void customCheckIntervalCrossesTheBridgeWithItsUnit()
    {
        ApplicationController controller(false);
        SpeecherBridge *bridge = [[SpeecherBridge alloc] initWithController:&controller];
        SettingsSchemaModel *schema = bridge.settingsSchema;
        [schema setValue:@"custom" forRowId:@"updateCheckInterval"];
        [schema setValue:@{@"number": @8, @"unit": @"hours"} forRowId:@"updateCheckCustomInterval"];
        [schema commit];

        SettingsRowModel *row = settingsRow(schema, @"updateCheckCustomInterval");
        QVERIFY(row);
        QCOMPARE(row.units.count, NSUInteger(3));
        QCOMPARE(row.units.lastObject.maximum, NSInteger(30));
        QVERIFY([row.value isEqual:(@{@"number": @8, @"unit": @"hours"})]);
        QCOMPARE(controller.settings()->updateCheckIntervalMinutes(), 480);
    }

    void accountOptionsUseUserFacingLanguage()
    {
        ApplicationController controller(false);
        SpeecherBridge *bridge = [[SpeecherBridge alloc] initWithController:&controller];
        SettingsRowModel *openAi = settingsRow(bridge.settingsSchema, @"openAiAuthMode");
        SettingsRowModel *anthropic = settingsRow(bridge.settingsSchema, @"anthropicAuthMode");
        QVERIFY(openAi);
        QVERIFY(anthropic);

        QStringList openAiLabels;
        for (RowOptionModel *option in openAi.options) {
            openAiLabels.append(QString::fromNSString(option.label));
        }
        QCOMPARE(openAiLabels,
                 QStringList({QStringLiteral("Automatic"),
                              QStringLiteral("API key from the Codex app"),
                              QStringLiteral("ChatGPT sign-in from the Codex app"),
                              QStringLiteral("API key from the environment"),
                              QStringLiteral("API key saved in Speecher"),
                              QStringLiteral("CLI Proxy API account")}));

        QStringList anthropicLabels;
        for (RowOptionModel *option in anthropic.options) {
            anthropicLabels.append(QString::fromNSString(option.label));
        }
        QCOMPARE(anthropicLabels,
                 QStringList({QStringLiteral("Claude Code sign-in"),
                              QStringLiteral("CLI Proxy API account")}));
    }

    void anthropicCredentialStatusFollowsTheAuthMode()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        ApplicationController controller(false);
        const QString credentialsPath = directory.filePath(QStringLiteral("credentials.json"));
        controller.settings()->raw().setValue(SettingsKeys::ClaudeCredentialsPath,
                                              credentialsPath);
        SpeecherBridge *bridge = [[SpeecherBridge alloc] initWithController:&controller];

        QVERIFY(bridge.anthropicCredentialStatus.text.length > 0);
        QVERIFY(!bridge.anthropicCredentialStatus.ready);
        __block bool credentialsChanged = false;
        bridge.anthropicCredentialsChanged = ^{ credentialsChanged = true; };
        QFile credentials(credentialsPath);
        QVERIFY(credentials.open(QIODevice::WriteOnly));
        QVERIFY(credentials.write(QByteArrayLiteral(
                    R"({"claudeAiOauth":{"accessToken":"token","expiresAt":4102444800000}})"))
                > 0);
        credentials.close();

        QTRY_VERIFY_WITH_TIMEOUT(credentialsChanged, 2000);
        QCOMPARE(QString::fromNSString(bridge.anthropicCredentialStatus.text),
                 QStringLiteral("Signed in with Claude Code"));
        QVERIFY(bridge.anthropicCredentialStatus.ready);
        [bridge.settingsSchema setValue:@"cliproxy" forRowId:@"anthropicAuthMode"];
        QCOMPARE(bridge.anthropicCredentialStatus.text.length, NSUInteger(0));
    }

    // A Carbon hotkey is consumed system-wide and never reaches a recorder's
    // key monitor: recording must let go of the registration and take it back
    // when recording ends, or pressing the bound combination while recording
    // starts dictation instead of re-recording it.
    void overlappingShortcutRecordingsRestoreAfterTheLastEnds()
    {
        ApplicationController controller(false);
        SpeecherBridge *bridge = [[SpeecherBridge alloc] initWithController:&controller];
        // An obscure combination, so nothing else on a CI host holds it.
        QVERIFY(controller.setGlobalShortcut(
            QKeySequence(Qt::META | Qt::ALT | Qt::SHIFT | Qt::Key_F9)));
        QVERIFY(!hotKeyComboIsFree());

        [bridge beginShortcutRecording];
        [bridge beginShortcutRecording];
        QVERIFY(hotKeyComboIsFree());
        // Deferred startup must not restore a shortcut while it is recorded.
        controller.frontEndReady();
        QCoreApplication::processEvents();
        QVERIFY(hotKeyComboIsFree());

        [bridge endShortcutRecordingFailedRole:nil];
        QVERIFY(hotKeyComboIsFree());
        const unichar replacement = NSF10FunctionKey;
        QVERIFY([bridge bindShortcutWithCharacters:[NSString stringWithCharacters:&replacement length:1]
                                     modifierFlags:NSEventModifierFlagControl
                                                   | NSEventModifierFlagOption
                                                   | NSEventModifierFlagShift] == nil);
        QVERIFY(hotKeyComboIsFree(kVK_F10));
        [bridge endShortcutRecordingFailedRole:nil];
        QVERIFY(hotKeyComboIsFree());
        QVERIFY(!hotKeyComboIsFree(kVK_F10));
    }

    void shortcutCleanupAfterControllerDestruction()
    {
        SpeecherBridge *bridge;
        {
            ApplicationController controller(false);
            bridge = [[SpeecherBridge alloc] initWithController:&controller];
            [bridge beginShortcutRecording];
        }
        // Recorder callbacks may arrive after controller teardown.
        [bridge beginShortcutRecording];
        QVERIFY([bridge endShortcutRecordingFailedRole:nil] == nil);
    }

    // Ending a recording that bound a replacement keeps the replacement rather
    // than restoring the suspended combination over it.
    void endingARecordingKeepsAShortcutBoundDuringIt()
    {
        ApplicationController controller(false);
        SpeecherBridge *bridge = [[SpeecherBridge alloc] initWithController:&controller];
        QVERIFY(controller.setGlobalShortcut(
            QKeySequence(Qt::META | Qt::ALT | Qt::SHIFT | Qt::Key_F9)));

        [bridge beginShortcutRecording];
        QVERIFY([bridge bindShortcutWithCharacters:@"g"
                                     modifierFlags:NSEventModifierFlagControl
                                                   | NSEventModifierFlagOption] == nil);
        [bridge endShortcutRecordingFailedRole:nil];

        QCOMPARE(controller.globalShortcut().combination(),
                 QKeySequence(Qt::META | Qt::ALT | Qt::Key_G));
        QVERIFY(hotKeyComboIsFree());
    }

    void endingShortcutRecordingReportsRegistrationConflict()
    {
        ApplicationController controller(false);
        SpeecherBridge *bridge = [[SpeecherBridge alloc] initWithController:&controller];
        QVERIFY(controller.setGlobalShortcut(
            QKeySequence(Qt::META | Qt::ALT | Qt::SHIFT | Qt::Key_F9)));
        [bridge beginShortcutRecording];
        EventHotKeyRef competingHotKey = nullptr;
        const auto cleanup = qScopeGuard([&] {
            if (competingHotKey) UnregisterEventHotKey(competingHotKey);
        });
        const EventHotKeyID identifier{'spct', 100};
        QCOMPARE(RegisterEventHotKey(kVK_F9, controlKey | optionKey | shiftKey,
                                     identifier, GetApplicationEventTarget(),
                                     kEventHotKeyExclusive, &competingHotKey), OSStatus(noErr));
        SpeecherShortcutRole failedRole = SpeecherShortcutRolePause;
        NSString *error = [bridge endShortcutRecordingFailedRole:&failedRole];
        QVERIFY(error.length > 0);
        QCOMPARE(int(failedRole), int(SpeecherShortcutRoleDictation));
    }

    // Only the Cancel and Pause Shortcuts take a bare key such as C or
    // Escape, and they leave it free between Dictation Sessions.
    void sessionShortcutsTakeBareKeys()
    {
        ApplicationController controller(false);
        SpeecherBridge *bridge = [[SpeecherBridge alloc] initWithController:&controller];
        const auto cleanup = qScopeGuard([&] {
            [bridge clearShortcutForRole:SpeecherShortcutRoleCancel];
            [bridge clearShortcutForRole:SpeecherShortcutRolePause];
        });
        QVERIFY([bridge bindShortcutWithCharacters:@"c" modifierFlags:0 role:SpeecherShortcutRoleDictation] != nil);

        const unichar escape = 0x1b;
        const struct {
            SpeecherShortcutRole role;
            NSString *characters;
            UInt32 keyCode;
            Qt::Key key;
        } cases[] = {
            {SpeecherShortcutRoleCancel, @"c", kVK_ANSI_C, Qt::Key_C},
            {SpeecherShortcutRolePause, [NSString stringWithCharacters:&escape length:1], kVK_Escape, Qt::Key_Escape},
        };
        for (const auto &bareKey : cases) {
            QVERIFY(settingsRow(bridge.settingsSchema, [SpeecherBridge rowIdForShortcutRole:bareKey.role]));
            QVERIFY([bridge bindShortcutWithCharacters:bareKey.characters modifierFlags:0 role:bareKey.role] == nil);
            QCOMPARE(controller.globalShortcut(GlobalShortcutRole(bareKey.role)).combination(),
                     QKeySequence(bareKey.key));
            QCOMPARE(QString::fromNSString([bridge shortcutDisplayForRole:bareKey.role]),
                     ShortcutBinding(QKeySequence(bareKey.key)).displayText());
            QVERIFY(hotKeyComboIsFree(bareKey.keyCode, 0));
        }
    }

    // A Cancel or Pause binder holds its hot key only while armed and not
    // suspended; the dictation shortcut still needs a modifier.
    void sessionShortcutHotKeyIsHeldOnlyWhileArmed()
    {
        for (const GlobalShortcutRole role : {GlobalShortcutRole::Cancel, GlobalShortcutRole::Pause}) {
            MacSessionShortcutBinder binder(GlobalShortcutBinder::actionFor(role));
            const auto cleanup = qScopeGuard([&] { binder.setShortcut({}); });
            QVERIFY(binder.setShortcut(QKeySequence(Qt::Key_Escape)));
            QVERIFY(hotKeyComboIsFree(kVK_Escape, 0));
            binder.setArmed(true);
            QVERIFY(!hotKeyComboIsFree(kVK_Escape, 0));
            binder.suspend();
            QVERIFY(hotKeyComboIsFree(kVK_Escape, 0));
            QVERIFY(binder.resume().isEmpty());
            QVERIFY(!hotKeyComboIsFree(kVK_Escape, 0));
            binder.setArmed(false);
            QVERIFY(hotKeyComboIsFree(kVK_Escape, 0));
        }
        MacGlobalShortcutBinder dictation(GlobalShortcutBinder::actionFor(GlobalShortcutRole::Dictation));
        QString error;
        QVERIFY(!dictation.setShortcut(QKeySequence(Qt::Key_Escape), &error));
        QVERIFY(!error.isEmpty());
    }

    // The recorder binds before it ends the recording, so a Cancel or Pause
    // Shortcut is set while suspended. It still refuses keys another app
    // holds, and takes nothing it was not asked to hold.
    void sessionShortcutSetWhileSuspendedStillRefusesTakenKeys()
    {
        MacSessionShortcutBinder binder(GlobalShortcutBinder::actionFor(GlobalShortcutRole::Cancel));
        const auto cleanup = qScopeGuard([&] { binder.setShortcut({}); });
        binder.suspend();
        EventHotKeyRef competingHotKey = nullptr;
        const EventHotKeyID identifier{'spct', 101};
        QCOMPARE(RegisterEventHotKey(kVK_F9, controlKey | optionKey | shiftKey,
                                     identifier, GetApplicationEventTarget(),
                                     kEventHotKeyExclusive, &competingHotKey), OSStatus(noErr));
        const auto releaseCompeting = qScopeGuard([&] { UnregisterEventHotKey(competingHotKey); });
        QString error;
        QVERIFY(!binder.setShortcut(QKeySequence(Qt::META | Qt::ALT | Qt::SHIFT | Qt::Key_F9), &error));
        QVERIFY(!error.isEmpty());

        QVERIFY(binder.setShortcut(QKeySequence(Qt::Key_Escape)));
        QVERIFY(hotKeyComboIsFree(kVK_Escape, 0));
        binder.setArmed(true);
        QVERIFY(hotKeyComboIsFree(kVK_Escape, 0));
        QVERIFY(binder.resume().isEmpty());
        QVERIFY(!hotKeyComboIsFree(kVK_Escape, 0));
        binder.setArmed(false);
    }

    // NSEvent monitors cannot stop a key, so a Cancel or Pause single key
    // that types is refused whatever the Accessibility grant says. One that
    // cannot type follows the grant, as the dictation shortcut's does.
    void sessionShortcutsRefuseASingleKeyThatTypes()
    {
        ApplicationController controller(false);
        SpeecherBridge *bridge = [[SpeecherBridge alloc] initWithController:&controller];
        const auto cleanup = qScopeGuard([&] {
            [bridge clearShortcutForRole:SpeecherShortcutRoleCancel];
            [bridge clearShortcutForRole:SpeecherShortcutRolePause];
        });
        const ShortcutBinding typingKey = ShortcutBinding::singleKey(QStringLiteral("KeyC"));
        const struct {
            SpeecherShortcutRole role;
            NSString *silentKey;
        } cases[] = {{SpeecherShortcutRoleCancel, @"F13"}, {SpeecherShortcutRolePause, @"F14"}};
        for (const auto &session : cases) {
            QCOMPARE(QString::fromNSString([bridge bindSingleKeyCode:@"KeyC" role:session.role]),
                     watchedKeyStillTypesText(typingKey));
            QVERIFY(controller.globalShortcut(GlobalShortcutRole(session.role)).isEmpty());
            QCOMPARE([bridge bindSingleKeyCode:session.silentKey role:session.role] == nil,
                     bool(AXIsProcessTrusted()));
        }
    }

    // The single-key half of the bridge: keycode-to-name mapping, display,
    // the non-blocking warnings, and the per-binding refusal. Binding is
    // asserted both ways because the answer follows the Accessibility grant:
    // CI seeds it, a bare runner does not, and either way silence is wrong.
    void singleKeyBridgeSurfaceMapsWarnsAndBinds()
    {
        ApplicationController controller(false);
        SpeecherBridge *bridge = [[SpeecherBridge alloc] initWithController:&controller];

        QCOMPARE(QString::fromNSString([bridge keyCodeNameForMacKeyCode:kVK_RightOption]),
                 QStringLiteral("AltRight"));
        QVERIFY([bridge keyCodeNameForMacKeyCode:0x7FFF] == nil);
        QCOMPARE(QString::fromNSString([bridge displayForSingleKeyCode:@"AltRight"]),
                 QStringLiteral("Right Option"));

        // Option's caveat is accents, a typing key's is that it still types,
        // and a silent key carries none. All of them would save.
        QVERIFY([[bridge warningForSingleKeyCode:@"AltRight"] containsString:@"accent"]);
        QVERIFY([bridge warningForSingleKeyCode:@"KeyE"].length > 0);
        QCOMPARE([bridge warningForSingleKeyCode:@"F13"].length, NSUInteger(0));

        // A key Mac keyboards lack is refused whatever the grant says.
        QVERIFY([bridge unsupportedReasonForSingleKeyCode:@"PrintScreen"] != nil);

        NSString *reason = [bridge unsupportedReasonForSingleKeyCode:@"AltRight"];
        if (AXIsProcessTrusted()) {
            QVERIFY(reason == nil);
            QVERIFY([bridge bindSingleKeyCode:@"AltRight"] == nil);
            QCOMPARE(QString::fromNSString(bridge.shortcutDisplay),
                     QStringLiteral("Right Option"));
            QVERIFY(controller.globalShortcut().isSingleKey());
        } else {
            QVERIFY(reason != nil);
            QVERIFY([bridge bindSingleKeyCode:@"AltRight"] != nil);
            QVERIFY(!controller.globalShortcut().isSingleKey());
        }
    }

    // The Sparkle user driver's callbacks arrive through the public driver
    // seam, so the state machine walks here without an appcast.
    void sparkleDriverSeamMapsStates()
    {
        ApplicationController controller(false);
        auto *updates = qobject_cast<MacSparkleUpdater *>(controller.updates());
        QVERIFY(updates);
        QSignalSpy changed(updates, &UpdateController::changed);

        updates->driverCheckStarted();
        QCOMPARE(updates->state(), UpdateController::State::Checking);
        // A user-initiated check reports its progress in the banner.
        QVERIFY(updates->bannerVisible());

        bool installRequested = false;
        updates->driverUpdateFound(QStringLiteral("9.9.9"), 9900,
                                   [&installRequested](MacSparkleUpdater::Reply reply) {
                                       installRequested = reply == MacSparkleUpdater::Reply::Install;
                                   });
        QCOMPARE(updates->state(), UpdateController::State::UpdateAvailable);
        QCOMPARE(updates->availableVersion(), QStringLiteral("9.9.9"));
        // A stable offer displays as its number; a nightly names its build and
        // commit from the appcast, or the date when no build number rides along.
        QCOMPARE(updates->availableVersionDisplay(), QStringLiteral("9.9.9"));
        updates->driverUpdateFound(QStringLiteral("9.9.10-nightly.20260921+gabc1234"), 9901,
                                   [](MacSparkleUpdater::Reply) {});
        QCOMPARE(updates->availableVersionDisplay(),
                 QStringLiteral("nightly build 9901 (gabc1234)"));
        updates->driverUpdateFound(QStringLiteral("9.9.9"), 9900,
                                   [&installRequested](MacSparkleUpdater::Reply reply) {
                                       installRequested = reply == MacSparkleUpdater::Reply::Install;
                                   });
        QVERIFY(updates->bannerVisible());

        updates->updateNow();
        QVERIFY(installRequested);

        updates->driverDownloadStarted();
        QCOMPARE(updates->state(), UpdateController::State::Downloading);
        QVERIFY(updates->bannerVisible());
        updates->driverDownloadExpects(200);
        updates->driverDownloadReceived(50);
        QCOMPARE(updates->downloadPercent(), 25);

        bool installReplied = false;
        updates->driverReadyToRestart(
            [&installReplied](MacSparkleUpdater::Reply) { installReplied = true; });
        QCOMPARE(updates->state(), UpdateController::State::ReadyToRestart);
        QCOMPARE(updates->downloadPercent(), 100);
        QVERIFY(updates->bannerVisible());
        // Ready is an offer, not an order: nothing restarts until asked.
        QVERIFY(!installReplied);

        updates->driverFailed(QStringLiteral("The download failed"));
        QCOMPARE(updates->state(), UpdateController::State::Error);
        QCOMPARE(updates->errorMessage(), QStringLiteral("The download failed"));
        QVERIFY(updates->bannerVisible());
        QVERIFY(changed.count() >= 6);
    }

    void dismissedVersionSuppressesTheBanner()
    {
        ApplicationController controller(false);
        auto *updates = qobject_cast<MacSparkleUpdater *>(controller.updates());
        QVERIFY(updates);

        bool dismissed = false;
        updates->driverUpdateFound(QStringLiteral("9.9.9"), 9900,
                                   [&dismissed](MacSparkleUpdater::Reply reply) {
                                       dismissed = reply == MacSparkleUpdater::Reply::Dismiss;
                                   });
        QVERIFY(updates->bannerVisible());

        updates->dismissAvailableVersion();
        QVERIFY(dismissed);
        QCOMPARE(controller.settings()->updatesDismissedVersion(), QStringLiteral("9.9.9"));
        updates->driverSessionEnded();
        QCOMPARE(updates->state(), UpdateController::State::Idle);

        // Found again, the dismissed version is answered rather than held: an
        // unanswered reply would keep Sparkle in a session and block every later
        // check. It stays silent and does not hold the session open.
        bool reDismissed = false;
        updates->driverUpdateFound(QStringLiteral("9.9.9"), 9900,
                                   [&reDismissed](MacSparkleUpdater::Reply reply) {
                                       reDismissed = reply == MacSparkleUpdater::Reply::Dismiss;
                                   });
        QVERIFY(reDismissed);
        QVERIFY(updates->state() != UpdateController::State::UpdateAvailable);
        QVERIFY(!updates->bannerVisible());

        // A newer version still surfaces.
        updates->driverSessionEnded();
        updates->driverUpdateFound(QStringLiteral("10.0.0"), 10000, [](MacSparkleUpdater::Reply) {});
        QCOMPARE(updates->state(), UpdateController::State::UpdateAvailable);
        QVERIFY(updates->bannerVisible());
    }

    // A channel switch mid-download abandons the old channel's in-flight update
    // rather than installing and restarting into the channel just left.
    void channelSwitchDuringDownloadCancelsAndDisarms()
    {
        ApplicationController controller(false);
        controller.settings()->setUpdateChannel(UpdateChannel::Stable);
        auto *updates = qobject_cast<MacSparkleUpdater *>(controller.updates());
        QVERIFY(updates);

        updates->driverUpdateFound(QStringLiteral("9.9.9"), 9900, [](MacSparkleUpdater::Reply) {});
        updates->installAndRestart();
        bool downloadCancelled = false;
        updates->driverDownloadStarted([&downloadCancelled] { downloadCancelled = true; });
        QCOMPARE(updates->state(), UpdateController::State::Downloading);

        controller.settings()->setUpdateChannel(UpdateChannel::Nightly);
        QVERIFY(downloadCancelled);
        QCOMPARE(updates->state(), UpdateController::State::Idle);
        QVERIFY(!updates->bannerVisible());

        // The armed restart is disarmed: a later ready-to-restart must not
        // relaunch on its own into the abandoned download.
        bool installed = false;
        updates->driverReadyToRestart(
            [&installed](MacSparkleUpdater::Reply) { installed = true; });
        QVERIFY(!installed);
    }

    // A manual check gives feedback the standard Sparkle dialogs used to: it
    // shows progress, an up-to-date result, and a failure.
    void manualCheckSurfacesProgressResultAndFailure()
    {
        ApplicationController controller(false);
        auto *updates = qobject_cast<MacSparkleUpdater *>(controller.updates());
        QVERIFY(updates);

        updates->driverCheckStarted();
        QVERIFY(updates->bannerVisible());
        updates->driverUpToDate();
        QCOMPARE(updates->state(), UpdateController::State::UpToDate);
        QVERIFY(updates->bannerVisible());
        updates->dismissAvailableVersion();
        QCOMPARE(updates->state(), UpdateController::State::Idle);
        QVERIFY(!updates->bannerVisible());

        updates->driverCheckStarted();
        updates->driverFailed(QStringLiteral("You are offline"));
        QCOMPARE(updates->state(), UpdateController::State::CheckFailed);
        QVERIFY(updates->bannerVisible());
        QCOMPARE(updates->errorMessage(), QStringLiteral("You are offline"));
    }

    void installAndRestartWritesTheRestoreState()
    {
        ApplicationController controller(false);
        // The front end's UI goes with the controller, not with the outermost
        // pool, which never drains (NativeUi).
        const AutoreleasePool pool;
        MacFrontEnd frontEnd(&controller);
        controller.setFrontEnd(&frontEnd);
        auto *updates = qobject_cast<MacSparkleUpdater *>(controller.updates());
        QVERIFY(updates);

        frontEnd.showSettingsWindow();
        const auto teardown = qScopeGuard([&] {
            frontEnd.hideMainWindow();
            settle();
        });

        updates->driverUpdateFound(QStringLiteral("9.9.9"), 9900, [](MacSparkleUpdater::Reply) {});
        updates->installAndRestart();
        bool installRequested = false;
        updates->driverReadyToRestart(
            [&installRequested](MacSparkleUpdater::Reply reply) {
                installRequested = reply == MacSparkleUpdater::Reply::Install;
            });

        // The armed restart fires as soon as the install is ready, and the
        // relaunch is told to bring the settings window back.
        QVERIFY(installRequested);
        QCOMPARE(updates->state(), UpdateController::State::Restarting);
        QCOMPARE(controller.settings()->updatesRestoreState(), QStringLiteral("settings"));
    }

    // A CI-only capture: with SPEECHER_UPDATE_PREVIEW_DIR set, render the
    // update UI states to PNGs the workflow uploads. Skipped in a normal run,
    // so it neither slows the suite nor needs a display of its own.
    void renderUpdatePreviewsWhenRequested()
    {
        const QString directory = qEnvironmentVariable("SPEECHER_UPDATE_PREVIEW_DIR");
        if (directory.isEmpty()) {
            QSKIP("SPEECHER_UPDATE_PREVIEW_DIR unset; preview capture is CI-only");
        }
        NSMutableArray<SpeecherUpdateBanner *> *banners = [NSMutableArray array];
        for (SpeecherUpdatePreviewState state :
             {SpeecherUpdatePreviewStateAvailable, SpeecherUpdatePreviewStateDownloading,
              SpeecherUpdatePreviewStateReadyToRestart, SpeecherUpdatePreviewStateError,
              SpeecherUpdatePreviewStateManualInstall, SpeecherUpdatePreviewStateCheckFailed}) {
            [banners addObject:[SpeecherUpdateBanner previewForState:state]];
        }
        NSArray<NSString *> *written =
            [SpeecherUpdatePreview renderToDirectory:directory.toNSString()
                                             banners:banners
                                            whatsNew:[SpeecherWhatsNewBanner previewForVersion:@"0.2.0"]];
        QCOMPARE(written.count, NSUInteger(banners.count + 3));
        for (NSString *name in written) {
            const QString path = directory + QStringLiteral("/") + QString::fromNSString(name);
            QVERIFY2(QFile::exists(path), qPrintable(path));
        }
    }

    void whatsNewOfferFollowsPendingUpgradeState()
    {
        SettingsStore settings;
        settings.setUpdatesPendingWhatsNewVersion(QStringLiteral("0.1.0"));
        ApplicationController controller(false);
        NativeUi native(controller);
        SpeecherBridge *bridge = native.bridge;
        SpeecherMacUI *ui = native.ui;

        QVERIFY(ui.whatsNewOfferVisible);
        [bridge clearPendingWhatsNew];
        QVERIFY(!ui.whatsNewOfferVisible);
    }

    // The Rating and Advanced rows reach Swift with core's bars and models,
    // and leave the page with a provider that has none to show.
    void ratingRowsCrossTheBridge()
    {
        ApplicationController controller(false);
        SpeecherBridge *bridge = [[SpeecherBridge alloc] initWithController:&controller];
        SettingsSchemaModel *schema = bridge.settingsSchema;
        [schema setValue:@"codex" forRowId:@"speechProvider"];
        [schema setValue:@"anthropic" forRowId:@"refinementProvider"];
        [schema commit];

        SettingsRowModel *speech = settingsRow(schema, @"speechRating");
        QVERIFY(speech);
        QVERIFY(speech.kind == SpeecherRowKindRating);
        QCOMPARE(speech.ratings.count, NSUInteger(2));
        QCOMPARE(QString::fromNSString(speech.ratings[0].label), QStringLiteral("Accuracy"));
        QCOMPARE(speech.ratings[0].value.doubleValue, 8.5);
        QCOMPARE(QString::fromNSString(speech.ratings[0].valueText), QStringLiteral("8.5/10"));
        QCOMPARE(QString::fromNSString(speech.ratings[1].label), QStringLiteral("Speed"));
        QCOMPARE(QString::fromNSString(speech.ratings[1].valueText), QStringLiteral("7/10"));

        SettingsRowModel *models = settingsRow(schema, @"speechModels");
        QVERIFY(models);
        QVERIFY(models.kind == SpeecherRowKindModelList);
        QCOMPARE(models.ratedModels.count, NSUInteger(2));
        QCOMPARE(QString::fromNSString(models.ratedModels[0].name), QStringLiteral("GPT Live Transcribe"));
        QCOMPARE(QString::fromNSString(models.ratedModels[0].note), QStringLiteral("Writes each phrase as you pause."));
        QCOMPARE(QString::fromNSString(models.ratedModels[1].name), QStringLiteral("GPT Transcribe"));
        // A service's models have no bars of their own.
        QCOMPARE(models.ratedModels[0].bars.count, NSUInteger(0));

        SettingsRowModel *refinement = settingsRow(schema, @"refinementRating");
        QVERIFY(refinement);
        QCOMPARE(QString::fromNSString(refinement.ratings[0].label), QStringLiteral("Quality"));
        QCOMPARE(QString::fromNSString(refinement.ratings[0].valueText), QStringLiteral("10/10"));
        QCOMPARE(QString::fromNSString(refinement.ratings[1].valueText), QStringLiteral("4.5/10"));

        [schema setValue:@"endpoint" forRowId:@"speechProvider"];
        [schema setValue:@"endpoint" forRowId:@"refinementProvider"];
        [schema commit];
        QVERIFY(!settingsRow(schema, @"speechRating"));
        QVERIFY(!settingsRow(schema, @"speechModels"));
        QVERIFY(!settingsRow(schema, @"refinementRating"));
    }

    // The setup steps' bars and Advanced lists: a service's, a Local Model's
    // naming what was rated once the hardware is known, with every model
    // rated, and none for Custom Endpoint.
    void setupRatingsComeFromCore()
    {
        ApplicationController controller(false);
        SpeecherBridge *bridge = [[SpeecherBridge alloc] initWithController:&controller];

        SpeecherProviderRating *claude = [bridge setupProviderRating:SpeecherProviderRoleSpeech provider:@"claude"];
        QVERIFY(claude);
        QCOMPARE(QString::fromNSString(claude.bars[0].valueText), QStringLiteral("5.5/10"));
        QCOMPARE(QString::fromNSString(claude.bars[1].valueText), QStringLiteral("10/10"));
        QCOMPARE(claude.subject.length, NSUInteger(0));

        // Nothing is rated for this computer until the probe reads its memory.
        LocalSetupTestAccess::setHardware(*controller.localSetup(), HardwareProfile{});
        QVERIFY(![bridge setupProviderRating:SpeecherProviderRoleSpeech provider:@"local"]);
        HardwareProfile probed;
        probed.systemRamBytes = quint64(16) << 30;
        LocalSetupTestAccess::setHardware(*controller.localSetup(), probed);
        SpeecherProviderRating *local = [bridge setupProviderRating:SpeecherProviderRoleSpeech provider:@"local"];
        QVERIFY(local);
        QCOMPARE(local.bars.count, NSUInteger(2));
        QVERIFY([local.subject hasSuffix:@" on this computer"]);
        NSArray<SpeecherRatedModel *> *localModels = [bridge setupProviderModels:SpeecherProviderRoleSpeech
                                                                        provider:@"local"];
        QVERIFY(localModels.count > 1);
        for (SpeecherRatedModel *model in localModels) {
            QCOMPARE(model.bars.count, NSUInteger(2));
        }

        SpeecherProviderRating *openAi = [bridge setupProviderRating:SpeecherProviderRoleRefinement
                                                            provider:@"openai"];
        QVERIFY(openAi);
        QCOMPARE(QString::fromNSString(openAi.bars[0].valueText), QStringLiteral("10/10"));
        QCOMPARE(QString::fromNSString(openAi.bars[1].valueText), QStringLiteral("7/10"));
        NSArray<SpeecherRatedModel *> *openAiModels = [bridge setupProviderModels:SpeecherProviderRoleRefinement
                                                                         provider:@"openai"];
        QCOMPARE(openAiModels.count, NSUInteger(1));
        QCOMPARE(QString::fromNSString(openAiModels[0].note),
                 QStringLiteral("The default. Change it in Settings, under Refinement."));

        for (SpeecherProviderRole role : {SpeecherProviderRoleSpeech, SpeecherProviderRoleRefinement}) {
            QVERIFY(![bridge setupProviderRating:role provider:@"endpoint"]);
            QCOMPARE([bridge setupProviderModels:role provider:@"endpoint"].count, NSUInteger(0));
        }
    }
};

int runMacFrontEndTests(int argc, char **argv)
{
    MacFrontEndTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_mac_frontend.moc"
