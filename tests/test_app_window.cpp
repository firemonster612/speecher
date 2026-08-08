#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "ui/AppWindow.h"

#include <QComboBox>

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
};

int runAppWindowTests(int argc, char **argv)
{
    AppWindowTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_app_window.moc"
