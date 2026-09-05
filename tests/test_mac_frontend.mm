#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "core/settings/SettingsKeys.h"
#include "frontend/mac/MacFrontEnd.h"
#include "frontend/mac/SpeecherBridge.h"
#include "ui/AppWindow.h"
#include "ui/SetupAssistant.h"
#include "ui/TranscriberPopup.h"

#import <AppKit/AppKit.h>

// The Swift class's Objective-C runtime name is mangled, so a hand-written
// @interface cannot stand in for the generated header.
#import "SpeecherUI-Swift.h"

#include <QDeadlineTimer>
#include <QApplication>
#include <QFile>
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

} // namespace

class MacFrontEndTests : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        SettingsStore settings;
        settings.raw().clear();
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
