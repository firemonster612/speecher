#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "core/settings/SettingsKeys.h"
#include "frontend/mac/MacFrontEnd.h"
#include "frontend/mac/SpeecherBridge.h"
#include "ui/AppWindow.h"
#include "ui/SetupAssistant.h"
#include "ui/TranscriberPopup.h"
#include "ui/setup/SetupPages.h"

#import <AppKit/AppKit.h>

// The Swift class's Objective-C runtime name is mangled, so a hand-written
// @interface cannot stand in for the generated header.
#import "SpeecherUI-Swift.h"

#include <QAbstractButton>
#include <QDeadlineTimer>
#include <QApplication>
#include <QCheckBox>
#include <QFile>
#include <QTemporaryDir>

using namespace speecher;

namespace {

template<typename Widget>
Widget *widgetOfType()
{
    for (QWidget *widget : QApplication::allWidgets()) {
        if (auto *match = dynamic_cast<Widget *>(widget)) {
            return match;
        }
    }
    return nullptr;
}

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

// The setup assistant offers Skip setup as a wizard custom button or as a
// KAssistantDialog action button depending on the build, and only one of those
// exists to be named.
QAbstractButton *visibleButton(const QWidget *within, const QString &text)
{
    for (QAbstractButton *button : within->findChildren<QAbstractButton *>()) {
        if (button->isVisible() && button->text() == text) {
            return button;
        }
    }
    return nullptr;
}

bool nativeSettingsAreVisible()
{
    for (NSWindow *window in NSApp.windows) {
        // The settings window's autosave name, which is stable where its title
        // is the pane the sidebar happens to be on and is hidden anyway.
        if (window.visible && [window.frameAutosaveName isEqualToString:@"SpeecherSettingsV2"]) {
            return true;
        }
    }
    return false;
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

    void skippingSetupOpensTheNativeSettingsWindow()
    {
        const int existingQtWindows = widgetCount<AppWindow>();
        ApplicationController controller(false);
        MacFrontEnd frontEnd(&controller);
        controller.setFrontEnd(&frontEnd);

        controller.showSetupAssistant();
        SetupAssistant *assistant = widgetOfType<SetupAssistant>();
        QVERIFY(assistant);
        QAbstractButton *skip = visibleButton(assistant, QStringLiteral("Skip setup"));
        QVERIFY(skip);
        skip->click();

        QVERIFY(nativeSettingsAreVisible());
        QCOMPARE(widgetCount<AppWindow>(), existingQtWindows);
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
                 QStringLiteral("Claude Code OAuth credentials found"));
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

    void setupFinishesWithStartAtLoginPage()
    {
        ApplicationController controller(false);
        SetupAssistant assistant(&controller);
        const QList<int> ids = assistant.pageIds();

        QCOMPARE(ids.size(), 9);
        QWizardPage *last = assistant.page(ids.last());
        QCOMPARE(last->title(), QStringLiteral("Start at login"));
        auto *startAtLogin = last->findChild<QCheckBox *>(QStringLiteral("launchAtLogin"));
        QVERIFY(startAtLogin);
        QVERIFY(startAtLogin->isChecked());
    }

    void setupWelcomeDoesNotDescribeTheLinuxFinishFlow()
    {
        ApplicationController controller(false);
        SetupAssistant assistant(&controller);
        WelcomeSetupPage *welcome = nullptr;
        for (QWidget *widget : assistant.findChildren<QWidget *>()) {
            if (auto *page = dynamic_cast<WelcomeSetupPage *>(widget)) {
                welcome = page;
                break;
            }
        }
        QVERIFY(welcome);
        for (const QLabel *label : welcome->findChildren<QLabel *>()) {
            QVERIFY(!label->text().contains(
                QStringLiteral("ends by setting up a Global Shortcut")));
        }
    }
};

int runMacFrontEndTests(int argc, char **argv)
{
    MacFrontEndTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_mac_frontend.moc"
