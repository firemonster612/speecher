#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "core/OutputMethod.h"
#include "core/SettingsStore.h"
#include "dictation/DictationSession.h"
#include "frontend/qt/QtFrontEnd.h"
#include "ui/AppPage.h"
#include "ui/AppWindow.h"
#include "ui/DictationPage.h"
#include "ui/settings/SettingsPageSet.h"
#include "ui/Theme.h"
#ifdef Q_OS_LINUX
#include "ui/setup/LinuxGlobalShortcutSetupPage.h"
#endif

#include <QComboBox>
#include <QAbstractItemDelegate>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QFormLayout>
#include <QFrame>
#include <QClipboard>
#include <QCheckBox>
#include <QGuiApplication>
#include <QIcon>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSplitter>
#include <QStandardPaths>
#include <QStackedWidget>
#include <QVBoxLayout>

#ifdef SPEECHER_WITH_KPAGEWIDGET
#include <KPageWidget>
#endif

using namespace speecher;

namespace {

#ifdef SPEECHER_WITH_KPAGEWIDGET
KPageWidget *sidebar(AppWindow &window)
{
    return window.findChild<KPageWidget *>(QStringLiteral("appNavigation"));
}

KPageWidgetItem *sidebarPage(AppWindow &window, int row)
{
    auto *pages = sidebar(window);
    auto *model = qobject_cast<KPageWidgetModel *>(pages->model());
    return model->item(model->index(row, 0));
}

QListView *sidebarView(AppWindow &window)
{
    return window.findChild<QListView *>(QStringLiteral("appNavigationView"));
}

void setSidebarRow(AppWindow &window, int row)
{
    sidebar(window)->setCurrentPage(sidebarPage(window, row));
}

int sidebarCurrentRow(AppWindow &window)
{
    auto *pages = sidebar(window);
    auto *model = qobject_cast<KPageWidgetModel *>(pages->model());
    return model->index(pages->currentPage()).row();
}

QString sidebarCurrentTitle(AppWindow &window)
{
    return sidebar(window)->currentPage()->name();
}

bool sidebarRowHidden(AppWindow &window, int row)
{
    return sidebarView(window)->isRowHidden(row);
}

QIcon sidebarIcon(AppWindow &window, int row)
{
    return sidebarPage(window, row)->icon();
}

QWidget *currentSidebarPageWidget(AppWindow &window)
{
    return sidebar(window)->currentPage()->widget();
}
#else
QListWidget *sidebar(AppWindow &window)
{
    return window.findChild<QListWidget *>(QStringLiteral("appNavigation"));
}

void setSidebarRow(AppWindow &window, int row)
{
    sidebar(window)->setCurrentRow(row);
}

int sidebarCurrentRow(AppWindow &window)
{
    return sidebar(window)->currentRow();
}

QString sidebarCurrentTitle(AppWindow &window)
{
    return sidebar(window)->currentItem()->text();
}

bool sidebarRowHidden(AppWindow &window, int row)
{
    return sidebar(window)->item(row)->isHidden();
}

QIcon sidebarIcon(AppWindow &window, int row)
{
    return sidebar(window)->item(row)->icon();
}

QWidget *currentSidebarPageWidget(AppWindow &window)
{
    return window.findChild<QStackedWidget *>()->currentWidget();
}
#endif

} // namespace

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
            setSidebarRow(window, page);
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
            QStringLiteral("Accounts"),
            QStringLiteral("Refinement"),
            QStringLiteral("Vocabulary"),
        };
        AppWindow window(&controller);
        QCOMPARE(window.pageCount(), 8);
        QCOMPARE(window.pageTitles(), titles);
    }

    void sidebarUsesThePlatformPageWidget()
    {
        ApplicationController controller(true);
        AppWindow window(&controller);
#ifdef SPEECHER_WITH_KPAGEWIDGET
        auto *navigation = window.findChild<KPageWidget *>(QStringLiteral("appNavigation"));
        QVERIFY(navigation);
        QCOMPARE(navigation->faceType(), KPageView::FlatList);
        QCOMPARE(sidebarView(window)->itemDelegate()->objectName(),
                 QStringLiteral("viewItemPositionSidebarDelegate"));
#else
        auto *navigation = window.findChild<QListWidget *>(QStringLiteral("appNavigation"));
        QVERIFY(navigation);
        QCOMPARE(navigation->itemDelegate()->objectName(),
                 QStringLiteral("viewItemPositionSidebarDelegate"));
#endif
    }

#ifdef Q_OS_LINUX
    void generalSettingsContainsTheGlobalShortcutEditor()
    {
        ApplicationController controller(true);
        AppWindow window(&controller);

        LinuxGlobalShortcutSetupPage *control = nullptr;
        for (QWidget *widget : window.findChildren<QWidget *>()) {
            if (auto *page = dynamic_cast<LinuxGlobalShortcutSetupPage *>(widget)) {
                control = page;
                break;
            }
        }
        QVERIFY(control);
        auto *integration = control->findChild<QWidget *>(
            QStringLiteral("appMenuIntegration"));
        QVERIFY(integration);
        QVERIFY(integration->isHidden());

        bool hasFullWidthHeading = false;
        for (const QLabel *label : window.findChildren<QLabel *>(
                 QStringLiteral("subsectionLabel"))) {
            hasFullWidthHeading = hasFullWidthHeading
                || label->text() == QStringLiteral("Global Shortcut");
        }
        QVERIFY(hasFullWidthHeading);
    }

    void sidebarOffersQuitSpeecher()
    {
        ApplicationController controller(true);
        AppWindow window(&controller);
        auto *quit = window.findChild<QPushButton *>(QStringLiteral("quitSpeecher"));
        QVERIFY(quit);
        QCOMPARE(quit->text(), QStringLiteral("Quit Speecher"));
        QSignalSpy requested(&controller, SIGNAL(quitRequested()));
        QVERIFY(requested.isValid());
        quit->click();
        QCOMPARE(requested.count(), 1);
    }
#endif

    void startupDesktopIntegrationWaitsForFirstWindowExposure()
    {
        ApplicationController controller(true);
        QtFrontEnd frontEnd(&controller);
        controller.setFrontEnd(&frontEnd);
        QSignalSpy accessibilityChanged(
            &controller,
            &ApplicationController::accessibilityStateChanged);

        QTest::qWait(20);
        QCOMPARE(accessibilityChanged.count(), 0);

        controller.showMainWindow();
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
        // The change must not save immediately (that is the debounce), but
        // waiting out the 600ms timer races slow CI runners, so drive the
        // pending autosave deterministically instead.
        QCOMPARE(controller.settings()->theme(), QStringLiteral("light"));
        window.flushPendingAutoSave();
        QCOMPARE(controller.settings()->theme(),
                 Theme::overrideHonored() ? QStringLiteral("dark")
                                          : QStringLiteral("system"));
    }

    void settingsDeletionCancelsPendingAutoSave()
    {
        ApplicationController controller(true);
        controller.settings()->setTheme(QStringLiteral("light"));
        AppWindow window(&controller);
        auto *theme = window.findChild<QComboBox *>(QStringLiteral("themeControl"));
        auto *pages = window.findChild<SettingsPageSet *>();
        QVERIFY(theme);
        QVERIFY(pages);
        window.show();
        QTRY_COMPARE_WITH_TIMEOUT(theme->currentData().toString(), QStringLiteral("light"), 250);

        theme->setCurrentIndex(theme->findData(QStringLiteral("dark")));
        pages->prepareForSettingsDeletion();
        controller.settings()->raw().clear();
        controller.settings()->raw().sync();
        window.flushPendingAutoSave();

        QVERIFY(!controller.settings()->raw().contains(QStringLiteral("ui/theme")));
        QVERIFY(!pages->save(false, false));
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

    void dictationSummaryNamesTheShortcutAndOutputInUserTerms()
    {
        ApplicationController controller(true);
        DictationPage page(&controller);
        page.show();
        QCoreApplication::processEvents();

        // No Theme card; the slot shows the Global Shortcut and opens General.
        QVERIFY(!page.findChild<QLabel *>(QStringLiteral("themeSummary")));
        QLabel *shortcut = page.findChild<QLabel *>(QStringLiteral("shortcutSummary"));
        QVERIFY(shortcut);
        const QString expected = controller.globalShortcutDisplay().isEmpty()
            ? QString()
            : controller.globalShortcutDisplay();
        if (!expected.isEmpty()) {
            QCOMPARE(shortcut->property("fullText").toString(), expected);
        } else {
            QVERIFY(!shortcut->property("fullText").toString().isEmpty());
        }
        QWidget *card = shortcut->parentWidget();
        while (card && !card->property("navTarget").isValid()) {
            card = card->parentWidget();
        }
        QVERIFY(card);
        QSignalSpy navigate(&page, &DictationPage::navigateRequested);
        QTest::mouseClick(card, Qt::LeftButton);
        QCOMPARE(navigate.count(), 1);
        QCOMPARE(navigate.first().first().value<AppPageId>(), AppPageId::General);

        // The Output card names the chosen method, not the platform's status.
        QLabel *output = nullptr;
        for (QLabel *label : page.findChildren<QLabel *>()) {
            if (label->property("fullText").toString()
                == OutputMethod::label(controller.settings()->outputMethod())) {
                output = label;
            }
        }
        QVERIFY(output);
    }

    void dictationTranscriptStaysReadOnlyAndCopies()
    {
        ApplicationController controller(true);
        DictationPage page(&controller);
        page.show();
        QCoreApplication::processEvents();

        auto *transcript = page.findChild<QPlainTextEdit *>(QStringLiteral("dictationTranscript"));
        QVERIFY(transcript);
        // Edits would go nowhere, so the transcript never unlocks.
        QVERIFY(transcript->isReadOnly());
        page.setStatus(QStringLiteral("listening"));
        QVERIFY(transcript->isReadOnly());
        page.setStatus(QStringLiteral("idle"));
        QVERIFY(transcript->isReadOnly());
        QVERIFY(transcript->textInteractionFlags() & Qt::TextSelectableByMouse);

        transcript->setPlainText(QStringLiteral("hello transcript"));
        auto *copy = transcript->findChild<QToolButton *>(QStringLiteral("copyTranscript"));
        QVERIFY(copy);
        copy->click();
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("hello transcript"));
    }

    void dictationHasOneStartControlAndTheHeaderHasNone()
    {
        ApplicationController controller(true);
        AppWindow window(&controller);
        auto *header = window.findChild<QWidget *>(QStringLiteral("sidebarHeaderStrip"));
        QVERIFY(header);
        for (const QPushButton *button : header->findChildren<QPushButton *>()) {
            QVERIFY(button->text() != QStringLiteral("Start Dictation"));
        }

        int startControls = 0;
        for (const QPushButton *button : window.findChildren<QPushButton *>()) {
            startControls += button->text() == QStringLiteral("Start Dictation");
        }
        QCOMPARE(startControls, 1);
    }

    void dictationPageKeepsTheLastErrorUntilTheNextSession()
    {
        ApplicationController controller(true);
        DictationPage page(&controller);
        page.show();
        QCoreApplication::processEvents();

        auto *error = page.findChild<QLabel *>(QStringLiteral("dictationError"));
        QVERIFY(error);
        QVERIFY(!error->isVisible());

        const QString message = QStringLiteral("Claude login expired; sign in again with Claude Code");
        emit controller.session()->popupErrorRequested(message);
        page.setStatus(QStringLiteral("error"));
        QVERIFY(error->isVisible());
        QCOMPARE(error->text(), message);

        // Leaving the error state does not hide it; only a new session does.
        page.setStatus(QStringLiteral("idle"));
        QVERIFY(error->isVisible());
        page.setStatus(QStringLiteral("listening"));
        QVERIFY(!error->isVisible());
        QVERIFY(error->text().isEmpty());
    }

    void missingThemeIconsLeaveTextRatherThanADocumentIcon()
    {
        if (!QIcon::fromTheme(QStringLiteral("edit-paste")).isNull()) {
            QSKIP("An icon theme is installed, so nothing falls back here");
        }
        ApplicationController controller(true);
        AppWindow window(&controller);
        QVERIFY(sidebar(window));
        for (int row = 0; row < window.pageCount(); ++row) {
            QVERIFY2(sidebarIcon(window, row).isNull(),
                     qPrintable(window.pageTitles().at(row)));
        }
        auto *copy = window.findChild<QToolButton *>(QStringLiteral("copyTranscript"));
        QVERIFY(copy);
        QCOMPARE(copy->text(), QStringLiteral("Copy"));
        QCOMPARE(copy->toolButtonStyle(), Qt::ToolButtonTextOnly);
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
        QCOMPARE(controller.settings()->theme(),
                 Theme::overrideHonored() ? QStringLiteral("dark")
                                          : QStringLiteral("system"));
    }

    void savingAnotherPageDoesNotRevertWhatsNewSettings()
    {
        ApplicationController controller(true);
        QWidget parent;
        SchemaContext context;
        context.currentVersion = QStringLiteral("0.1.0");
        context.lastSeenVersion = QStringLiteral("0.0.0");
        SettingsPageSet pages(&controller, &parent, buildSettingsSchema(context));
        pages.loadBeforeShow();

        auto *autoCheck = pages.whatsNew()->findChild<QCheckBox *>(
            QStringLiteral("autoCheckUpdates"));
        QVERIFY(autoCheck);
        autoCheck->setChecked(false);
        QVERIFY(pages.save(false, false));

        auto *theme = pages.general()->findChild<QComboBox *>(QStringLiteral("themeControl"));
        QVERIFY(theme);
        theme->setCurrentIndex(theme->findData(QStringLiteral("dark")));
        QVERIFY(pages.save(false, false));

        QVERIFY(!controller.settings()->autoCheckUpdates());
    }

#ifndef Q_OS_MACOS
    // macOS takes its header colors from the system palette, so there is no
    // kdeglobals reader to exercise there.
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

        // Without the KDE platform theme the body is not drawn in KDE colours,
        // so the strip stays a shade of the active palette, whatever kdeglobals says.
        qApp->setProperty("KDE_COLOR_SCHEME_PATH", QVariant());
        {
            AppWindow plain(&controller);
            auto *plainStrip = plain.findChild<QWidget *>(QStringLiteral("sidebarHeaderStrip"));
            QVERIFY(plainStrip);
            QCOMPARE(plainStrip->palette().color(QPalette::Active, QPalette::Window),
                     plain.palette().color(QPalette::Active, QPalette::Window).darker(110));
        }

        // plasma-integration publishes the loaded scheme on the application.
        qApp->setProperty("KDE_COLOR_SCHEME_PATH", configPath);
        const auto clearProperty = qScopeGuard([] {
            qApp->setProperty("KDE_COLOR_SCHEME_PATH", QVariant());
        });
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
#endif

    void updateBannerDismissIsACloseToolButton()
    {
        ApplicationController controller(true);
        AppWindow window(&controller);
        auto *banner = window.findChild<QFrame *>(QStringLiteral("updateBanner"));
        QVERIFY(banner);
        auto *dismiss = banner->findChild<QToolButton *>(QStringLiteral("dismissUpdate"));
        QVERIFY(dismiss);
        QVERIFY(dismiss->text().isEmpty());
        QVERIFY(!dismiss->icon().isNull());
        QCOMPARE(dismiss->toolTip(), QStringLiteral("Dismiss"));
        // No text-glyph stand-in for a close button remains.
        for (const QPushButton *button : banner->findChildren<QPushButton *>()) {
            QVERIFY(button->text() != QStringLiteral("×"));
        }
    }

    void sidebarShellSupportsPageSearch()
    {
        ApplicationController controller(true);
        AppWindow window(&controller);
        auto *search = window.findChild<QLineEdit *>(QStringLiteral("appSearch"));
        QVERIFY(search);
#ifndef SPEECHER_WITH_KPAGEWIDGET
        QVERIFY(window.findChild<QSplitter *>());
#endif

        search->setText(QStringLiteral("Keep before speech"));
        QVERIFY(sidebarRowHidden(window, 1) && !sidebarRowHidden(window, 2));
    }

    void programmaticNavigationUpdatesShellChrome()
    {
        ApplicationController controller(true);
        AppWindow window(&controller);
        window.navigateToSettings(AppPageId::Output);
        QCOMPARE(sidebarCurrentTitle(window), QStringLiteral("Output"));
    }

    void sidebarCanReturnToThePageThatOpenedWhatsNew()
    {
        ApplicationController controller(true);
        AppWindow window(&controller);
        auto *whatsNew = window.findChild<QPushButton *>(QStringLiteral("whatsNew"));
        QVERIFY(sidebar(window) && whatsNew);

        setSidebarRow(window, 1);
        whatsNew->click();
        QCOMPARE(currentSidebarPageWidget(window),
                 static_cast<QWidget *>(window.findChild<SettingsPageSet *>()->whatsNew()));
        setSidebarRow(window, 1);
        QCOMPARE(sidebarCurrentRow(window), 1);
    }

    void whatsNewOffersAWayBackToThePageItWasOpenedFrom()
    {
        ApplicationController controller(true);
        AppWindow window(&controller);
        window.show();
        auto *whatsNew = window.findChild<QPushButton *>(QStringLiteral("whatsNew"));
        auto *back = window.findChild<QToolButton *>(QStringLiteral("whatsNewBack"));
        auto *title = window.findChild<QLabel *>(QStringLiteral("pageTitle"));
        QVERIFY(sidebar(window) && whatsNew && back && title);
        QVERIFY(!back->isVisible());

        // Opened from General, the same way the update banner opens it.
        setSidebarRow(window, 1);
        whatsNew->click();
        QCOMPARE(currentSidebarPageWidget(window),
                 static_cast<QWidget *>(window.findChild<SettingsPageSet *>()->whatsNew()));
        QCOMPARE(title->text(), QStringLiteral("What's New"));
        QVERIFY(back->isVisible());
#ifdef SPEECHER_WITH_KPAGEWIDGET
        QVERIFY(sidebarRowHidden(window, sidebarCurrentRow(window)));
#else
        QVERIFY(!sidebar(window)->currentItem());
#endif

        back->click();
        QCOMPARE(sidebarCurrentRow(window), 1);
        QCOMPARE(title->text(), QStringLiteral("General"));
        QVERIFY(!back->isVisible());
    }

    void saveReportsFailedValidator()
    {
        ApplicationController controller(true);
        QWidget parent;
        SettingsPageSet pages(&controller, &parent);
        SettingsPageSet::SaveOutcome outcome;
        controller.settings()->setPasteRules({
            {PasteRuleScope::Application, QStringLiteral("org.example.App"), PasteMethod::StandardPaste, true},
            {PasteRuleScope::Application, QStringLiteral("ORG.EXAMPLE.APP"), PasteMethod::ClipboardOnly, true},
        });
        pages.load();
        QVERIFY(!pages.save(false, true, &outcome));
        QCOMPARE(outcome.failure, SettingsPageSet::SaveFailure::DuplicatePasteRuleIds);
        QCOMPARE(outcome.messages,
                 QStringList{QStringLiteral("Each application ID can have only one paste rule.")});
    }

    void saveReportsInvalidReplacementRules()
    {
        ApplicationController controller(true);
        QWidget parent;
        SettingsPageSet pages(&controller, &parent);
        SettingsPageSet::SaveOutcome outcome;
        pages.load();

        // setBindingRules refuses invalid rules outright, so the only way to get
        // them in front of save() is to load them straight into the page.
        AppSettings withDuplicateBinding = controller.settings()->snapshot();
        withDuplicateBinding.bindings = {
            {QStringLiteral("my email"), QStringLiteral("one")},
            {QStringLiteral("MY email"), QStringLiteral("two")},
        };
        pages.bindings()->load(withDuplicateBinding);

        QVERIFY(!pages.save(false, true, &outcome));
        QCOMPARE(outcome.failure, SettingsPageSet::SaveFailure::InvalidReplacementRules);
        QCOMPARE(outcome.messages,
                 QStringList{QStringLiteral(
                     "Row 2 duplicates the normalized spoken phrase from row 1.")});
    }
};

int runAppWindowTests(int argc, char **argv)
{
    AppWindowTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_app_window.moc"
