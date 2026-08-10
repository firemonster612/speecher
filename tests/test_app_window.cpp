#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "ui/AppPage.h"
#include "ui/AppWindow.h"
#include "ui/settings/BindingsSettingsPage.h"
#include "ui/settings/SettingsPageSet.h"

#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QSplitter>

using namespace speecher;

class AppWindowTests : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        SettingsStore settings;
        settings.raw().clear();
    }

    void sidebarShellConstructsWithSharedPageTitles()
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
        AppWindow window(&controller);
        QCOMPARE(window.pageCount(), 7);
        QCOMPARE(window.pageTitles(), titles);
    }

    void sidebarAutoSavesAfterDebounce()
    {
        ApplicationController controller(true);
        controller.settings()->setTheme(QStringLiteral("light"));
        AppWindow window(&controller);
        auto *theme = window.findChild<QComboBox *>(QStringLiteral("themeControl"));
        QVERIFY(theme);
        QCOMPARE(theme->currentData().toString(), QStringLiteral("system"));
        window.show();
        QTRY_COMPARE_WITH_TIMEOUT(theme->currentData().toString(), QStringLiteral("light"), 250);
        theme->setCurrentIndex(theme->findData(QStringLiteral("dark")));
        QTRY_COMPARE_WITH_TIMEOUT(controller.settings()->theme(), QStringLiteral("dark"), 1500);
    }

    void sidebarFlushesPendingAutoSaveOnClose()
    {
        ApplicationController controller(true);
        controller.settings()->setTheme(QStringLiteral("system"));
        AppWindow window(&controller);
        auto *theme = window.findChild<QComboBox *>(QStringLiteral("themeControl"));
        QVERIFY(theme);
        window.show();
        QTRY_COMPARE_WITH_TIMEOUT(theme->currentData().toString(), QStringLiteral("system"), 250);
        theme->setCurrentIndex(theme->findData(QStringLiteral("dark")));
        window.close();
        QCOMPARE(controller.settings()->theme(), QStringLiteral("dark"));
    }

    void sidebarShellSupportsPageSearch()
    {
        ApplicationController controller(true);
        AppWindow window(&controller);
        auto *search = window.findChild<QLineEdit *>(QStringLiteral("appSearch"));
        auto *navigation = window.findChild<QListWidget *>(QStringLiteral("appNavigation"));
        QVERIFY(window.findChild<QSplitter *>() && search);

        search->setText(QStringLiteral("Pre-roll"));
        QVERIFY(navigation && navigation->item(1)->isHidden()
                && !navigation->item(2)->isHidden());
    }

    void programmaticNavigationUpdatesShellChrome()
    {
        ApplicationController controller(true);
        AppWindow window(&controller);
        window.navigateToSettings(AppPageId::Output);
        QCOMPARE(window.findChild<QListWidget *>(QStringLiteral("appNavigation"))->currentItem()->text(),
                 QStringLiteral("Output"));
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
