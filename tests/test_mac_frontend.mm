#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "app/MacSparkleUpdater.h"
#include "core/AppSettings.h"
#include "core/SettingsStore.h"
#include "core/settings/SettingsKeys.h"
#include "frontend/mac/MacFrontEnd.h"
#include "frontend/mac/SpeecherBridge.h"
#include "ui/AppWindow.h"
#include "ui/SetupAssistant.h"
#include "ui/TranscriberPopup.h"

#import <AppKit/AppKit.h>
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

// Whether the given ⌃⌥⇧ function key is unregistered system-wide: Carbon's exclusive
// option refuses the registration while anyone — including this process's own
// shortcut binder — holds the combination.
bool hotKeyComboIsFree(UInt32 keyCode = kVK_F9)
{
    const EventHotKeyID identifier{'spct', 99};
    EventHotKeyRef probe = nullptr;
    const OSStatus status = RegisterEventHotKey(keyCode,
                                                controlKey | optionKey | shiftKey,
                                                identifier,
                                                GetApplicationEventTarget(),
                                                kEventHotKeyExclusive,
                                                &probe);
    if (status == noErr && probe) {
        UnregisterEventHotKey(probe);
    }
    return status == noErr;
}

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
        MacFrontEnd frontEnd(&controller);

        QCOMPARE(widgetCount<TranscriberPopup>(), existingPopups);
    }

    void nativeDictationProblemCanBeDismissed()
    {
        ApplicationController controller(false);
        SpeecherBridge *bridge = [[SpeecherBridge alloc] initWithController:&controller];
        SpeecherMacUI *ui = [[SpeecherMacUI alloc] initWithBridge:bridge];

        [ui showDictationProblem:@"The microphone stopped"];
        QVERIFY(ui.dictationPanelVisible);

        [ui dismissDictationPanel];
        QVERIFY(!ui.dictationPanelVisible);
    }

    void nativeDictationPanelUsesStatusWindowLevel()
    {
        ApplicationController controller(false);
        SpeecherBridge *bridge = [[SpeecherBridge alloc] initWithController:&controller];
        SpeecherMacUI *ui = [[SpeecherMacUI alloc] initWithBridge:bridge];

        QCOMPARE(ui.dictationPanelLevel, NSInteger(NSStatusWindowLevel));
    }

    void popupPresentationAcknowledgesRequestedGeneration()
    {
        ApplicationController controller(false);
        SpeecherBridge *bridge = [[SpeecherBridge alloc] initWithController:&controller];
        SpeecherMacUI *ui = [[SpeecherMacUI alloc] initWithBridge:bridge];
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
        SpeecherBridge *bridge = [[SpeecherBridge alloc] initWithController:&controller];
        SpeecherMacUI *ui = [[SpeecherMacUI alloc] initWithBridge:bridge];
        bridge.popupStatusChanged(@"Listening");
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
        const auto cleanup = qScopeGuard([&] { [ui dismissDictationPanel]; });
        const NSRect initial = panel.frame;
        QCOMPARE(initial.size.height, CGFloat(48));
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
        bridge.popupPreviewChanged(@"short preview");
        settle();
        QVERIFY(panel.frame.size.height > initial.size.height);
        QCOMPARE(panel.frame.origin.y, initial.origin.y);
        QVERIFY(capture("short-preview"));
        bridge.popupPreviewChanged(@"We should probably move the meeting to Thursday afternoon, after everyone has reviewed the latest draft.");
        settle();
        QVERIFY(panel.frame.size.width <= 488);
        QCOMPARE(panel.frame.origin.y, initial.origin.y);
        QVERIFY(capture("long-preview"));
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
        bridge.popupStatusChanged(@"Stopping");
        settle();
        QCOMPARE(panel.frame.size.height, initial.size.height);
        QVERIFY(capture("transcribing"));
        bridge.popupRefiningChanged(true);
        bridge.popupRefinementPreviewChanged(@"Move the meeting to Thursday afternoon.");
        settle();
        QVERIFY(panel.frame.size.height > initial.size.height);
        QCOMPARE(panel.frame.origin.y, initial.origin.y);
        QVERIFY(capture("refining"));
        QVERIFY(qAbs(NSMidX(panel.frame) - NSMidX(initial)) <= 1);
        bridge.popupFrozenChanged(false);
        bridge.popupOAuthRefreshRequested();
        settle();
        QCOMPARE(panel.frame.size.height, initial.size.height);
        QVERIFY(panel.frame.size.width < 200);
        QVERIFY(capture("renewal"));
        bridge.popupListeningIndicatorRequested();
        settle();
        QCOMPARE(panel.frame.size.width, initial.size.width);
    }

    // Skip, all nine pages, and Finish are driven through the native AX tree
    // in macOS setup assistant E2E. This catches a Qt wizard returning here.
    void setupUsesANativeWindow()
    {
        const int existingQtAssistants = widgetCount<SetupAssistant>();
        const int existingQtWindows = widgetCount<AppWindow>();
        ApplicationController controller(false);
        MacFrontEnd frontEnd(&controller);
        controller.setFrontEnd(&frontEnd);

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

    void outputMethodsOfferAccessibilityInsertion()
    {
        ApplicationController controller(false);
        SpeecherBridge *bridge = [[SpeecherBridge alloc] initWithController:&controller];
        SettingsRowModel *row = settingsRow(bridge.settingsSchema, @"outputMethod");
        QVERIFY(row);

        bool found = false;
        for (RowOptionModel *option in row.options) {
            found = found || [option.rowOptionId isEqualToString:@"direct_insert"];
        }
        QVERIFY(found);
    }

    void automaticDownloadsAppearForSparkle()
    {
        ApplicationController controller(false);
        SpeecherBridge *bridge = [[SpeecherBridge alloc] initWithController:&controller];
        SettingsRowModel *row = settingsRow(bridge.settingsSchema, @"autoInstallUpdates");

        QVERIFY(row);
        QVERIFY([row.help containsString:@"Sparkle"]);
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

        QVERIFY(bridge.anthropicCredentialStatus.length > 0);
        __block bool credentialsChanged = false;
        bridge.anthropicCredentialsChanged = ^{ credentialsChanged = true; };
        QFile credentials(credentialsPath);
        QVERIFY(credentials.open(QIODevice::WriteOnly));
        QVERIFY(credentials.write(QByteArrayLiteral(
                    R"({"claudeAiOauth":{"accessToken":"token","expiresAt":4102444800000}})"))
                > 0);
        credentials.close();

        QTRY_VERIFY_WITH_TIMEOUT(credentialsChanged, 2000);
        QCOMPARE(QString::fromNSString(bridge.anthropicCredentialStatus),
                 QStringLiteral("Signed in with Claude Code"));
        [bridge.settingsSchema setValue:@"cliproxy" forRowId:@"anthropicAuthMode"];
        QCOMPARE(bridge.anthropicCredentialStatus.length, NSUInteger(0));
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

        [bridge endShortcutRecording];
        QVERIFY(hotKeyComboIsFree());
        const unichar replacement = NSF10FunctionKey;
        QVERIFY([bridge bindShortcutWithCharacters:[NSString stringWithCharacters:&replacement length:1]
                                     modifierFlags:NSEventModifierFlagControl
                                                   | NSEventModifierFlagOption
                                                   | NSEventModifierFlagShift] == nil);
        QVERIFY(hotKeyComboIsFree(kVK_F10));
        [bridge endShortcutRecording];
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
        QVERIFY([bridge endShortcutRecording] == nil);
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
        [bridge endShortcutRecording];

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
        NSString *error = [bridge endShortcutRecording];
        QVERIFY(error.length > 0);
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
        updates->driverUpdateFound(QStringLiteral("9.9.9"),
                                   [&installRequested](MacSparkleUpdater::Reply reply) {
                                       installRequested = reply == MacSparkleUpdater::Reply::Install;
                                   });
        QCOMPARE(updates->state(), UpdateController::State::UpdateAvailable);
        QCOMPARE(updates->availableVersion(), QStringLiteral("9.9.9"));
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
        updates->driverUpdateFound(QStringLiteral("9.9.9"),
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
        updates->driverUpdateFound(QStringLiteral("9.9.9"),
                                   [&reDismissed](MacSparkleUpdater::Reply reply) {
                                       reDismissed = reply == MacSparkleUpdater::Reply::Dismiss;
                                   });
        QVERIFY(reDismissed);
        QVERIFY(updates->state() != UpdateController::State::UpdateAvailable);
        QVERIFY(!updates->bannerVisible());

        // A newer version still surfaces.
        updates->driverSessionEnded();
        updates->driverUpdateFound(QStringLiteral("10.0.0"), [](MacSparkleUpdater::Reply) {});
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

        updates->driverUpdateFound(QStringLiteral("9.9.9"), [](MacSparkleUpdater::Reply) {});
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
        MacFrontEnd frontEnd(&controller);
        controller.setFrontEnd(&frontEnd);
        auto *updates = qobject_cast<MacSparkleUpdater *>(controller.updates());
        QVERIFY(updates);

        frontEnd.showSettingsWindow();

        updates->driverUpdateFound(QStringLiteral("9.9.9"), [](MacSparkleUpdater::Reply) {});
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

    // A CI-only capture: with SPEECHER_UPDATE_PREVIEW_DIR set, render the five
    // update UI states to PNGs the workflow uploads. Skipped in a normal run,
    // so it neither slows the suite nor needs a display of its own.
    void renderUpdatePreviewsWhenRequested()
    {
        const QString directory = qEnvironmentVariable("SPEECHER_UPDATE_PREVIEW_DIR");
        if (directory.isEmpty()) {
            QSKIP("SPEECHER_UPDATE_PREVIEW_DIR unset; preview capture is CI-only");
        }
        NSArray<NSString *> *written =
            [SpeecherUpdatePreview renderToDirectory:directory.toNSString()];
        QCOMPARE(written.count, NSUInteger(5));
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
        SpeecherBridge *bridge = [[SpeecherBridge alloc] initWithController:&controller];
        SpeecherMacUI *ui = [[SpeecherMacUI alloc] initWithBridge:bridge];

        QVERIFY(ui.whatsNewOfferVisible);
        [bridge clearPendingWhatsNew];
        QVERIFY(!ui.whatsNewOfferVisible);
    }
};

int runMacFrontEndTests(int argc, char **argv)
{
    MacFrontEndTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_mac_frontend.moc"
