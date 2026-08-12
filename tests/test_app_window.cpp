#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "ui/AppPage.h"
#include "ui/AppWindow.h"
#include "ui/DictationPage.h"
#include "ui/settings/BindingsSettingsPage.h"
#include "ui/settings/SettingsPageSet.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSplitter>
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

    void dictationSummaryTitleSharesFieldVerticalCenter()
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
            if (candidate->text() == QStringLiteral("Refinement:")) {
                title = candidate;
                break;
            }
        }
        QVERIFY(title);
        QLabel *value = page->findChild<QLabel *>(QStringLiteral("refinementSummary"));
        QVERIFY(value);

        const int titleCenter = title->mapTo(page, title->rect().center()).y();
        const int valueCenter = value->mapTo(page, value->rect().center()).y();
        QVERIFY(qAbs(titleCenter - valueCenter) <= 1);
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
        QCOMPARE(strip->palette().color(QPalette::Inactive, QPalette::Window), QColor(40, 50, 60));

        QVERIFY(writeConfig(
            "[WM]\nactiveBackground=15,25,35\nactiveForeground=215,216,217\n"
            "inactiveBackground=45,55,65\ninactiveForeground=175,176,177\n"));
        QTRY_COMPARE_WITH_TIMEOUT(strip->palette().color(QPalette::Active, QPalette::Window),
                                  QColor(15, 25, 35),
                                  500);
        QCOMPARE(strip->palette().color(QPalette::Inactive, QPalette::Window),
                 QColor(45, 55, 65));
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
