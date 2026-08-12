#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "ui/AppPage.h"
#include "ui/AppWindow.h"
#include "ui/DictationPage.h"
#include "ui/settings/BindingsSettingsPage.h"
#include "ui/settings/SettingsPageSet.h"
#include "ui/settings/SettingsPageSupport.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QVBoxLayout>

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

    void startupDesktopIntegrationWaitsForFirstWindowExposure()
    {
        ApplicationController controller(true);
        QSignalSpy accessibilityChanged(
            &controller,
            &ApplicationController::accessibilityStateChanged);

        QTest::qWait(20);
        QCOMPARE(accessibilityChanged.count(), 0);

        AppWindow window(&controller);
        window.show();
        QTRY_COMPARE_WITH_TIMEOUT(accessibilityChanged.count(), 1, 250);
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

    void dictationSummaryDefersSavedMicrophoneResolutionUntilShow()
    {
        ApplicationController controller(true);
        AppSettings settings = controller.settings()->snapshot();
        settings.audio.deviceId = QStringLiteral("saved-device");
        controller.settings()->applySnapshot(settings);

        AppWindow window(&controller);
        auto *microphone = window.findChild<QLabel *>(QStringLiteral("microphoneSummary"));
        QVERIFY(microphone);
        QCOMPARE(microphone->property("fullText").toString(),
                 QStringLiteral("Selected microphone"));
    }

    void dictationSummaryUsesOneNavigableRiverRow()
    {
        ApplicationController controller(true);
        QWidget surface;
        surface.setStyleSheet(QStringLiteral("QPushButton { min-height: 40px; }"));
        auto *layout = new QVBoxLayout(&surface);
        auto *page = new DictationPage(&controller, &surface);
        layout->addWidget(page);
        surface.show();
        QCoreApplication::processEvents();

        QLabel *title = nullptr;
        for (QLabel *candidate : page->findChildren<QLabel *>()) {
            if (candidate->text() == QStringLiteral("Refinement")) {
                title = candidate;
                break;
            }
        }
        QVERIFY(title);
        QLabel *value = page->findChild<QLabel *>(QStringLiteral("refinementSummary"));
        QVERIFY(value);

        QWidget *row = title;
        while (row && row->objectName() != QStringLiteral("settingsRow")) {
            row = row->parentWidget();
        }
        QVERIFY(row);
        QVERIFY(row->isAncestorOf(value));
    }

    void sidebarFlushesPendingAutoSaveOnClose()
    {
        ApplicationController controller(true);
        controller.settings()->setTheme(QStringLiteral("light"));
        AppWindow window(&controller);
        auto *theme = window.findChild<QComboBox *>(QStringLiteral("themeControl"));
        QVERIFY(theme);
        window.show();
        QCOMPARE(theme->currentData().toString(), QStringLiteral("light"));
        theme->setCurrentIndex(theme->findData(QStringLiteral("dark")));
        window.close();
        QCOMPARE(controller.settings()->theme(), QStringLiteral("dark"));
    }

    void headerStripKeepsHeightAcrossPages()
    {
        ApplicationController controller(true);
        AppWindow window(&controller);
        // Desktop styles (Breeze) pad buttons taller than the page title;
        // reproduce that so hiding the toggle would shrink the strip.
        window.setStyleSheet(QStringLiteral("QPushButton { min-height: 40px; }"));
        window.show();
        QCoreApplication::processEvents();

        auto *strip = window.findChild<QWidget *>(QStringLiteral("sidebarHeaderStrip"));
        auto *navigation = window.findChild<QListWidget *>(QStringLiteral("appNavigation"));
        QVERIFY(strip && navigation);
        const int dictationHeight = strip->height();

        navigation->setCurrentRow(1);
        QCoreApplication::processEvents();
        QCOMPARE(strip->height(), dictationHeight);
    }

    void dictationToggleLivesInTheDictationPage()
    {
        ApplicationController controller(true);
        AppWindow window(&controller);
        window.show();
        QCoreApplication::processEvents();

        auto *header = window.findChild<QWidget *>(QStringLiteral("sidebarHeaderStrip"));
        auto *stack = window.findChild<QStackedWidget *>(QStringLiteral("appPageStack"));
        QPushButton *toggle = nullptr;
        for (QPushButton *button : window.findChildren<QPushButton *>()) {
            if (button->text() == QStringLiteral("Start Dictation")) {
                toggle = button;
                break;
            }
        }

        QVERIFY(header && stack && toggle);
        QVERIFY(!header->isAncestorOf(toggle));
        QVERIFY(stack->currentWidget()->isAncestorOf(toggle));
    }

    void settingsPagesUseKirigamiRiverStructure()
    {
        ApplicationController controller(true);
        AppWindow window(&controller);
        window.resize(900, 640);
        window.show();
        QCoreApplication::processEvents();
        auto *stack = window.findChild<QStackedWidget *>(QStringLiteral("appPageStack"));
        QVERIFY(stack);

        for (int index = 0; index < stack->count(); ++index) {
            QWidget *page = stack->widget(index);
            QWidget *river = page->objectName() == QStringLiteral("settingsRiver")
                ? page
                : page->findChild<QWidget *>(QStringLiteral("settingsRiver"));
            QVERIFY2(river, qPrintable(QStringLiteral("Page %1 has no settings river").arg(index)));
            QCOMPARE(river->maximumWidth(), 560);
            QVERIFY2(river->findChild<QLabel *>(QStringLiteral("sectionLabel")),
                     qPrintable(QStringLiteral("Page %1 has no group heading").arg(index)));
            QVERIFY2(river->findChild<QWidget *>(QStringLiteral("settingsCard")),
                     qPrintable(QStringLiteral("Page %1 has no settings island").arg(index)));
        }
    }

    void headerStripTracksActiveAndInactiveKdeColors()
    {
        const QString configPath =
            QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QStringLiteral("/kdeglobals");
        QDir().mkpath(QFileInfo(configPath).absolutePath());
        QFile existing(configPath);
        const bool hadExistingConfig = existing.open(QIODevice::ReadOnly);
        const QByteArray previousConfig = hadExistingConfig ? existing.readAll() : QByteArray();
        const auto restoreConfig = qScopeGuard([=] {
            if (!hadExistingConfig) {
                QFile::remove(configPath);
                return;
            }
            QFile restored(configPath);
            if (restored.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                restored.write(previousConfig);
            }
        });

        const auto writeConfig = [&configPath](const QByteArray &contents) {
            QSaveFile config(configPath);
            if (!config.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                return false;
            }
            return config.write(contents) == contents.size() && config.commit();
        };

        QVERIFY(writeConfig(
            "[Colors:Header]\nBackgroundNormal=10,20,30\nForegroundNormal=220,221,222\n"
            "[Colors:Header][Inactive]\nBackgroundNormal=40,50,60\nForegroundNormal=180,181,182\n"));
        ApplicationController controller(true);
        AppWindow window(&controller);
        auto *strip = window.findChild<QWidget *>(QStringLiteral("sidebarHeaderStrip"));
        QVERIFY(strip);
        QCOMPARE(strip->palette().color(QPalette::Active, QPalette::Window), QColor(10, 20, 30));
        QCOMPARE(strip->palette().color(QPalette::Active, QPalette::WindowText),
                 QColor(220, 221, 222));
        QCOMPARE(strip->palette().color(QPalette::Inactive, QPalette::Window), QColor(40, 50, 60));
        QCOMPARE(strip->palette().color(QPalette::Inactive, QPalette::WindowText),
                 QColor(180, 181, 182));

        QVERIFY(writeConfig(
            "[Colors:Header]\nBackgroundNormal=70,80,90\nForegroundNormal=210,211,212\n"
            "[Colors:Header][Inactive]\nBackgroundNormal=100,110,120\nForegroundNormal=170,171,172\n"));
        QTRY_COMPARE_WITH_TIMEOUT(strip->palette().color(QPalette::Active, QPalette::Window),
                                  QColor(70, 80, 90),
                                  500);
        QCOMPARE(strip->palette().color(QPalette::Inactive, QPalette::Window),
                 QColor(100, 110, 120));

        QVERIFY(writeConfig(
            "[WM]\nactiveBackground=15,25,35\nactiveForeground=215,216,217\n"
            "inactiveBackground=45,55,65\ninactiveForeground=175,176,177\n"));
        QTRY_COMPARE_WITH_TIMEOUT(strip->palette().color(QPalette::Active, QPalette::Window),
                                  QColor(15, 25, 35),
                                  500);
        QCOMPARE(strip->palette().color(QPalette::Active, QPalette::WindowText),
                 QColor(215, 216, 217));
        QCOMPARE(strip->palette().color(QPalette::Inactive, QPalette::Window),
                 QColor(45, 55, 65));
        QCOMPARE(strip->palette().color(QPalette::Inactive, QPalette::WindowText),
                 QColor(175, 176, 177));
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
