#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "frontend/mac/MacFrontEnd.h"
#include "frontend/mac/SpeecherBridge.h"
#include "ui/AppWindow.h"
#include "ui/SetupAssistant.h"
#include "ui/TranscriberPopup.h"

#import <AppKit/AppKit.h>

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>

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
};

int runMacFrontEndTests(int argc, char **argv)
{
    MacFrontEndTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_mac_frontend.moc"
