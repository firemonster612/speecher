#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "ui/AppPage.h"
#include "ui/AppWindow.h"
#include "ui/settings/BindingsSettingsPage.h"
#include "ui/settings/SettingsPageSet.h"

#include <QAction>
#include <QComboBox>
#include <QListWidget>

using namespace speecher;

class AppWindowTests : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        SettingsStore settings;
        settings.raw().clear();
    }

    void shellsConstructWithSharedPageTitles()
    {
        ApplicationController controller(true);
        const QStringList titles{
            QStringLiteral("Dictation"),
            QStringLiteral("General"),
            QStringLiteral("Audio"),
            QStringLiteral("Applications"),
            QStringLiteral("Output"),
            QStringLiteral("Refinement"),
            QStringLiteral("Vocabulary"),
        };
        for (const QString &prototype : {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}) {
            AppWindow window(&controller, prototype);
            QCOMPARE(window.prototype(), prototype);
            QCOMPARE(window.pageCount(), 7);
            QCOMPARE(window.pageTitles(), titles);
        }
    }

    void prototypePersistsAcrossStores()
    {
        SettingsStore settings;
        QCOMPARE(settings.uiPrototype(), QStringLiteral("a"));
        settings.setUiPrototype(QStringLiteral("c"));
        SettingsStore reopened;
        QCOMPARE(reopened.uiPrototype(), QStringLiteral("c"));
    }

    void sidebarAutoSavesAfterDebounce()
    {
        ApplicationController controller(true);
        controller.settings()->setUiPrototype(QStringLiteral("a"));
        controller.settings()->setTheme(QStringLiteral("system"));
        AppWindow window(&controller, QStringLiteral("a"));
        auto *theme = window.findChild<QComboBox *>(QStringLiteral("themeControl"));
        QVERIFY(theme);
        theme->setCurrentIndex(theme->findData(QStringLiteral("dark")));
        QTRY_COMPARE_WITH_TIMEOUT(controller.settings()->theme(), QStringLiteral("dark"), 1500);
    }

    void sidebarFlushesPendingAutoSaveOnClose()
    {
        ApplicationController controller(true);
        controller.settings()->setTheme(QStringLiteral("system"));
        AppWindow window(&controller, QStringLiteral("a"));
        auto *theme = window.findChild<QComboBox *>(QStringLiteral("themeControl"));
        QVERIFY(theme);
        theme->setCurrentIndex(theme->findData(QStringLiteral("dark")));
        window.show();
        window.close();
        QCOMPARE(controller.settings()->theme(), QStringLiteral("dark"));
    }

    void programmaticNavigationUpdatesShellChrome()
    {
        ApplicationController controller(true);
        AppWindow sidebar(&controller, QStringLiteral("a"));
        sidebar.navigateToSettings(AppPageId::Output);
        QCOMPARE(sidebar.findChild<QListWidget *>(QStringLiteral("appNavigation"))->currentItem()->text(),
                 QStringLiteral("Output"));

        AppWindow toolbar(&controller, QStringLiteral("b"));
        toolbar.navigateToSettings(AppPageId::Output);
        QAction *selected = nullptr;
        for (QAction *action : toolbar.findChildren<QAction *>()) {
            if (action->isChecked()) selected = action;
        }
        QVERIFY(selected);
        QCOMPARE(selected->text(), QStringLiteral("Output"));
    }

    void saveReportsFailedValidator()
    {
        ApplicationController controller(true);
        QWidget parent;
        SettingsPageSet pages(&controller, &parent);
        pages.bindings()->load({
            {QStringLiteral("my,email"), QStringLiteral("one")},
            {QStringLiteral("MY email"), QStringLiteral("two")},
        });
        SettingsPageSet::SaveFailure failure = SettingsPageSet::SaveFailure::None;
        QVERIFY(!pages.save(false, true, &failure));
        QCOMPARE(failure, SettingsPageSet::SaveFailure::InvalidReplacementRules);

        controller.settings()->setBindingRules({});
        controller.settings()->setPasteRules({
            {PasteRuleScope::Application, QStringLiteral("org.example.App"), PasteMethod::StandardPaste, true},
            {PasteRuleScope::Application, QStringLiteral("ORG.EXAMPLE.APP"), PasteMethod::ClipboardOnly, true},
        });
        pages.load();
        QVERIFY(!pages.save(false, true, &failure));
        QCOMPARE(failure, SettingsPageSet::SaveFailure::DuplicatePasteRuleIds);
    }
};

int runAppWindowTests(int argc, char **argv)
{
    AppWindowTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_app_window.moc"
