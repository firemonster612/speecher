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
#include <QFormLayout>
#include <QFrame>
#include <QClipboard>
#include <QGuiApplication>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
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

    void liveScreenshotPages()
    {
        const QString dir = qEnvironmentVariable("SPEECHER_TEST_SCREENSHOT_DIR");
        if (dir.isEmpty()) {
            QSKIP("Screenshot dump is opt-in");
        }
        ApplicationController controller(true);
        AppWindow window(&controller);
        window.resize(980, 680);
        window.show();
        QTest::qWait(200);
        for (int page = 0; page < window.pageCount(); ++page) {
            window.findChild<QListWidget *>(QStringLiteral("appNavigation"))->setCurrentRow(page);
            QTest::qWait(120);
            window.grab().save(QStringLiteral("%1/page-%2-%3.png")
                                   .arg(dir)
                                   .arg(page)
                                   .arg(window.pageTitles().at(page).toLower()));
        }
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
            QStringLiteral("Auth"),
            QStringLiteral("Refinement"),
            QStringLiteral("Vocabulary"),
        };
        AppWindow window(&controller);
        QCOMPARE(window.pageCount(), 8);
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

    void dictationSummaryCardsNavigate()
    {
        ApplicationController controller(true);
        DictationPage page(&controller);
        page.show();
        QCoreApplication::processEvents();

        QLabel *value = page.findChild<QLabel *>(QStringLiteral("refinementSummary"));
        QVERIFY(value);
        QWidget *card = value->parentWidget();
        while (card && !card->property("navTarget").isValid()) {
            card = card->parentWidget();
        }
        QVERIFY(card);

        QSignalSpy navigate(&page, &DictationPage::navigateRequested);
        QTest::mouseClick(card, Qt::LeftButton);
        QCOMPARE(navigate.count(), 1);
        QCOMPARE(navigate.first().first().value<AppPageId>(), AppPageId::Refinement);
    }

    void dictationTranscriptCopiesAndUnlocksAfterSession()
    {
        ApplicationController controller(true);
        DictationPage page(&controller);
        page.show();
        QCoreApplication::processEvents();

        auto *transcript = page.findChild<QPlainTextEdit *>(QStringLiteral("dictationTranscript"));
        QVERIFY(transcript);
        QVERIFY(!transcript->isReadOnly());
        page.setStatus(QStringLiteral("listening"));
        QVERIFY(transcript->isReadOnly());
        page.setStatus(QStringLiteral("refining"));
        QVERIFY(transcript->isReadOnly());
        page.setStatus(QStringLiteral("idle"));
        QVERIFY(!transcript->isReadOnly());

        transcript->setPlainText(QStringLiteral("hello transcript"));
        auto *copy = transcript->findChild<QToolButton *>(QStringLiteral("copyTranscript"));
        QVERIFY(copy);
        copy->click();
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("hello transcript"));
    }

    void dictationPageShowsHonestBusyActions()
    {
        ApplicationController controller(true);
        DictationPage page(&controller);

        page.setStatus(QStringLiteral("Refining"));
        QCOMPARE(page.toggleButton()->text(), QStringLiteral("Cancel Refinement"));
        QVERIFY(page.toggleButton()->isEnabled());

        page.setStatus(QStringLiteral("Stopping"));
        QCOMPARE(page.toggleButton()->text(), QStringLiteral("Stopping…"));
        QVERIFY(!page.toggleButton()->isEnabled());

        page.setStatus(QStringLiteral("Delivering"));
        QCOMPARE(page.toggleButton()->text(), QStringLiteral("Delivering…"));
        QVERIFY(!page.toggleButton()->isEnabled());
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
