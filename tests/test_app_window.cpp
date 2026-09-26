#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "app/UpdateController.h"
#include "core/InsightsLog.h"
#include "core/SettingsStore.h"
#include "dictation/DictationSession.h"
#include "core/TranscriptState.h"
#include "frontend/qt/QtFrontEnd.h"
#include "ui/AppPage.h"
#include "ui/AppWindow.h"
#include "ui/InlineMessage.h"
#include "ui/HomePage.h"
#include "ui/InsightsCharts.h"
#include "ui/settings/SettingsPageSet.h"
#include "ui/Theme.h"
#ifdef Q_OS_LINUX
#include "ui/setup/LinuxGlobalShortcutSetupPage.h"
#endif

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QToolTip>
#include <QSet>
#include <QLineEdit>
#include <QListWidget>
#include <QFormLayout>
#include <QFrame>
#include <QClipboard>
#include <QCheckBox>
#include <QGuiApplication>
#include <QIcon>
#include <QPushButton>
#include <QToolButton>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSplitter>
#include <QStandardPaths>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTemporaryDir>
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

    void homeRecoversBackgroundTranscript()
    {
        ApplicationController controller(true);
        auto *transcript = controller.session()->findChild<TranscriptState *>();
        QVERIFY(transcript);
        transcript->commitFinal(QStringLiteral("Words from the background session."));
        HomePage page(&controller);
        page.resize(900, 700);
        page.show();
        auto *last = page.findChild<QLabel *>(QStringLiteral("lastTranscript"));
        QVERIFY(last);
        QTRY_COMPARE(last->text(), QStringLiteral("Words from the background session."));
        auto *meta = page.findChild<QLabel *>(QStringLiteral("lastTranscriptMeta"));
        QCOMPARE(meta->text(), QStringLiteral("5 words"));
    }

    void sidebarShellConstructsWithSharedPageTitles()
    {
        ApplicationController controller(true);
        const QStringList titles{
            QStringLiteral("Home"),
            QStringLiteral("General"),
            QStringLiteral("Audio"),
            QStringLiteral("Output"),
            QStringLiteral("Accounts"),
            QStringLiteral("Refinement"),
            QStringLiteral("Vocabulary"),
        };
        AppWindow window(&controller);
        QCOMPARE(window.pageCount(), 7);
        QCOMPARE(window.pageTitles(), titles);
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
        controller.settings()->setUpdateChannel(UpdateChannel::Nightly);
        AppWindow window(&controller);
        auto *channel = window.findChild<QComboBox *>(QStringLiteral("updateChannel"));
        QVERIFY(channel);
        QCOMPARE(channel->currentData().toString(), QStringLiteral("stable"));
        window.show();
        QTRY_COMPARE_WITH_TIMEOUT(channel->currentData().toString(), QStringLiteral("nightly"), 250);
        channel->setCurrentIndex(channel->findData(QStringLiteral("stable")));
        // The change must not save immediately (that is the debounce), but
        // waiting out the 600ms timer races slow CI runners, so drive the
        // pending autosave deterministically instead.
        QCOMPARE(controller.settings()->updateChannel(), UpdateChannel::Nightly);
        window.flushPendingAutoSave();
        QCOMPARE(controller.settings()->updateChannel(), UpdateChannel::Stable);
    }

    void settingsDeletionCancelsPendingAutoSave()
    {
        ApplicationController controller(true);
        controller.settings()->setUpdateChannel(UpdateChannel::Nightly);
        AppWindow window(&controller);
        auto *channel = window.findChild<QComboBox *>(QStringLiteral("updateChannel"));
        auto *pages = window.findChild<SettingsPageSet *>();
        QVERIFY(channel);
        QVERIFY(pages);
        window.show();
        QTRY_COMPARE_WITH_TIMEOUT(channel->currentData().toString(), QStringLiteral("nightly"), 250);

        channel->setCurrentIndex(channel->findData(QStringLiteral("stable")));
        pages->prepareForSettingsDeletion();
        controller.settings()->raw().clear();
        controller.settings()->raw().sync();
        window.flushPendingAutoSave();

        QVERIFY(!controller.settings()->raw().contains(QStringLiteral("updates/channel")));
        QVERIFY(!pages->save(false, false));
    }

    void homeCopiesTheLastTranscript()
    {
        ApplicationController controller(true);
        controller.session()->findChild<TranscriptState *>()->commitFinal(
            QStringLiteral("hello transcript"));
        HomePage page(&controller);
        page.show();
        auto *copy = page.findChild<QToolButton *>(QStringLiteral("copyTranscript"));
        QVERIFY(copy);
        copy->click();
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("hello transcript"));
    }

    void homeShowsInsightsOnlyWhenOnAndRecorded()
    {
        QTemporaryDir dir;
        QFile seed(dir.filePath(QStringLiteral("seed.jsonl")));
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write(R"({"finishedAt":"2026-09-25T09:55:00","audioMs":38000,"words":90,"app":"Thunderbird","profile":"email"})"
                   "\n");
        seed.close();
        qputenv("SPEECHER_INSIGHTS_SEED", seed.fileName().toLocal8Bit());
        qputenv("SPEECHER_INSIGHTS_TODAY", "2026-09-26");
        const auto restore = qScopeGuard([] {
            qunsetenv("SPEECHER_INSIGHTS_SEED");
            qunsetenv("SPEECHER_INSIGHTS_TODAY");
        });
        ApplicationController controller(true);
        QCOMPARE(controller.insightsLog()->records().size(), 1);

        controller.settings()->setInsightsEnabled(false);
        HomePage page(&controller);
        page.resize(1000, 800);
        page.show();
        auto *notice = page.findChild<QFrame *>(QStringLiteral("insightsNotice"));
        QVERIFY(notice);
        QVERIFY(notice->isVisible());
        QCOMPARE(notice->findChild<QLabel *>(QStringLiteral("insightsNoticeTitle"))->text(),
                 QStringLiteral("Insights are off"));
        QVERIFY(page.findChildren<QFrame *>(QStringLiteral("insightTile")).isEmpty());
        QSignalSpy navigate(&page, &HomePage::navigateRequested);
        page.findChild<QPushButton *>(QStringLiteral("insightsNoticeSettings"))->click();
        QCOMPARE(navigate.count(), 1);
        QCOMPARE(navigate.first().first().value<AppPageId>(), AppPageId::General);

        controller.settings()->setInsightsEnabled(true);
        page.refresh();
        QVERIFY(!notice->isVisible());
        const QList<QFrame *> tiles = page.findChildren<QFrame *>(QStringLiteral("insightTile"));
        QCOMPARE(tiles.size(), 4);
        QStringList values;
        for (QFrame *tile : tiles) {
            values << tile->findChild<QLabel *>(QStringLiteral("insightValue"))->text();
        }
        QCOMPARE(values.first(), QStringLiteral("90"));
        QVERIFY(page.findChild<QWidget *>(QStringLiteral("activityHeatmap")));

        // Clearing the log leaves "No insights yet".
        controller.clearInsights();
        QVERIFY(notice->isVisible());
        QCOMPARE(notice->findChild<QLabel *>(QStringLiteral("insightsNoticeTitle"))->text(),
                 QStringLiteral("No insights yet"));
        QVERIFY(page.findChildren<QFrame *>(QStringLiteral("insightTile")).isEmpty());
    }

    void controllerKeepsTheRecordOfTheLastTranscript()
    {
        QTemporaryDir dir;
        const QString seed = dir.filePath(QStringLiteral("seed.jsonl"));
        QFile(seed).open(QIODevice::WriteOnly);
        qputenv("SPEECHER_INSIGHTS_SEED", seed.toLocal8Bit());
        const auto restore = qScopeGuard([] { qunsetenv("SPEECHER_INSIGHTS_SEED"); });
        ApplicationController controller(true);
        QSignalSpy changed(&controller, &ApplicationController::lastRecordChanged);
        const DictationRecord record{QDateTime(QDate(2026, 9, 26), QTime(9, 0)), 4000, 3,
                                     QStringLiteral("Kate"), WritingProfile::Other};

        emit controller.session()->transcriptDelivered(QStringLiteral("one two three"));
        emit controller.session()->dictationRecorded(record);
        QCOMPARE(controller.lastRecord()->appName, QStringLiteral("Kate"));

        // A delivery insights did not record leaves no record behind.
        emit controller.session()->transcriptDelivered(QStringLiteral("four"));
        QVERIFY(!controller.lastRecord());
        QCOMPARE(changed.count(), 2);

        // Clearing the history forgets the record the caption shows.
        emit controller.session()->dictationRecorded(record);
        QVERIFY(controller.clearInsights());
        QVERIFY(!controller.lastRecord());
    }

    void chartsDescribeTheCellUnderThePointerAtOnce()
    {
        // Hovering a heatmap day or an hour bar marks it and shows its tip
        // on the move itself, not after the platform's tooltip delay.
        const QDate today(2026, 9, 26);
        QList<HeatmapDay> days;
        for (int offset = 6; offset >= 0; --offset) {
            days.append({today.addDays(-offset), offset == 0 ? 3 : 1, 30, 60000});
        }
        InsightsHeatmap week(InsightsHeatmap::Shape::Week);
        week.setDays(days);
        week.resize(week.sizeHint());
        week.show();
        QVERIFY(QTest::qWaitForWindowExposed(&week));
        // Monday's dot: the first of seven equal columns, just under the top.
        const QPoint monday(week.width() / 14, 6);
        QTest::mouseMove(&week, monday);
        QTRY_VERIFY_WITH_TIMEOUT(QToolTip::isVisible(), 200);
        QVERIFY(QToolTip::text().contains(QStringLiteral("dictation")));
        QTest::mouseMove(&week, QPoint(week.width() - 1, week.height() - 1));
        QTRY_VERIFY_WITH_TIMEOUT(!QToolTip::isVisible(), 1000);

        InsightsBarChart hours;
        std::array<int, 24> counts{};
        counts[10] = 4;
        hours.setCounts(counts, 10);
        hours.resize(480, hours.sizeHint().height());
        hours.show();
        QVERIFY(QTest::qWaitForWindowExposed(&hours));
        QTest::mouseMove(&hours, QPoint(480 * 10 / 24 + 5, 10));
        QTRY_VERIFY_WITH_TIMEOUT(QToolTip::isVisible(), 200);
        QVERIFY(QToolTip::text().contains(QStringLiteral("4 dictations")));
    }

    void aNewSessionForgetsTheLastRecord()
    {
        // The session drops its last transcript when the next one starts,
        // delivered or not, so the caption's record must go with it: a
        // cancelled or failed session must not wear the previous one's app.
        QTemporaryDir dir;
        const QString seed = dir.filePath(QStringLiteral("seed.jsonl"));
        QFile(seed).open(QIODevice::WriteOnly);
        qputenv("SPEECHER_INSIGHTS_SEED", seed.toLocal8Bit());
        const auto restore = qScopeGuard([] { qunsetenv("SPEECHER_INSIGHTS_SEED"); });
        ApplicationController controller(true);
        emit controller.session()->dictationRecorded(
            {QDateTime(QDate(2026, 9, 26), QTime(9, 0)), 4000, 3, QStringLiteral("Kate"),
             WritingProfile::Other});
        QVERIFY(controller.lastRecord());
        controller.session()->toggle();
        QTRY_VERIFY(!controller.lastRecord());
    }

    void dictationHasOneStartControlAndTheHeaderHasNone()
    {
        ApplicationController controller(true);
        AppWindow window(&controller);
        auto *header = window.findChild<QWidget *>(QStringLiteral("sidebarHeaderStrip"));
        QVERIFY(header);
        QVERIFY(header->findChildren<QPushButton *>().isEmpty());

        int startControls = 0;
        for (const QPushButton *button : window.findChildren<QPushButton *>()) {
            startControls += button->text() == QStringLiteral("Start Dictation");
        }
        QCOMPARE(startControls, 1);
    }

    void homeKeepsTheLastErrorUntilTheNextSession()
    {
        ApplicationController controller(true);
        HomePage page(&controller);
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
        auto *navigation = window.findChild<QListWidget *>(QStringLiteral("appNavigation"));
        QVERIFY(navigation);
        for (int row = 0; row < navigation->count(); ++row) {
            QVERIFY2(navigation->item(row)->icon().isNull(),
                     qPrintable(navigation->item(row)->text()));
        }
        auto *copy = window.findChild<QToolButton *>(QStringLiteral("copyTranscript"));
        QVERIFY(copy);
        QCOMPARE(copy->text(), QStringLiteral("Copy"));
        QCOMPARE(copy->toolButtonStyle(), Qt::ToolButtonTextOnly);
    }

    void homeShowsHonestBusyActions()
    {
        ApplicationController controller(true);
        HomePage page(&controller);

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
        controller.settings()->setUpdateChannel(UpdateChannel::Nightly);
        AppWindow window(&controller);
        auto *channel = window.findChild<QComboBox *>(QStringLiteral("updateChannel"));
        QVERIFY(channel);
        window.show();
        QCOMPARE(channel->currentData().toString(), QStringLiteral("nightly"));
        channel->setCurrentIndex(channel->findData(QStringLiteral("stable")));
        window.close();
        QCOMPARE(controller.settings()->updateChannel(), UpdateChannel::Stable);
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

        auto *channel = pages.general()->findChild<QComboBox *>(QStringLiteral("updateChannel"));
        QVERIFY(channel);
        channel->setCurrentIndex(channel->findData(QStringLiteral("nightly")));
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

    void bannersAreKirigamiInlineMessages()
    {
        ApplicationController controller(true);
        AppWindow window(&controller);
        auto *banner = window.findChild<InlineMessage *>(QStringLiteral("updateBanner"));
        auto *warning = window.findChild<InlineMessage *>(QStringLiteral("autoSaveWarning"));
        QVERIFY(banner);
        QVERIFY(warning);
        QCOMPARE(warning->type(), InlineMessage::Type::Warning);
        auto *icon = banner->findChild<QLabel *>(QStringLiteral("inlineMessageIcon"));
        QVERIFY(icon);
        QVERIFY(!icon->pixmap().isNull());
        QVERIFY(banner->label()->wordWrap());
        // Each type has its own colour; the tint and border follow it.
        QSet<QRgb> colours;
        for (InlineMessage::Type type : {InlineMessage::Type::Information,
                                         InlineMessage::Type::Positive,
                                         InlineMessage::Type::Warning,
                                         InlineMessage::Type::Error}) {
            banner->setType(type);
            colours.insert(banner->typeColor().rgb());
        }
        QCOMPARE(colours.size(), 4);
    }

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
        auto *navigation = window.findChild<QListWidget *>(QStringLiteral("appNavigation"));
        QVERIFY(window.findChild<QSplitter *>() && search);

        search->setText(QStringLiteral("Keep before speech"));
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

    void sidebarCanReturnToThePageThatOpenedWhatsNew()
    {
        ApplicationController controller(true);
        AppWindow window(&controller);
        auto *navigation = window.findChild<QListWidget *>(QStringLiteral("appNavigation"));
        auto *stack = window.findChild<QStackedWidget *>();
        auto *whatsNew = window.findChild<QPushButton *>(QStringLiteral("whatsNew"));
        QVERIFY(navigation && stack && whatsNew);

        navigation->setCurrentRow(1);
        whatsNew->click();
        QCOMPARE(stack->currentIndex(), 7);
        navigation->setCurrentRow(1);
        QCOMPARE(stack->currentIndex(), 1);
    }

    void whatsNewOffersAWayBackToThePageItWasOpenedFrom()
    {
        ApplicationController controller(true);
        AppWindow window(&controller);
        window.show();
        auto *navigation = window.findChild<QListWidget *>(QStringLiteral("appNavigation"));
        auto *stack = window.findChild<QStackedWidget *>();
        auto *whatsNew = window.findChild<QPushButton *>(QStringLiteral("whatsNew"));
        auto *back = window.findChild<QToolButton *>(QStringLiteral("whatsNewBack"));
        auto *title = window.findChild<QLabel *>(QStringLiteral("pageTitle"));
        QVERIFY(navigation && stack && whatsNew && back && title);
        QVERIFY(!back->isVisible());

        // Opened from General, the same way the update banner opens it.
        navigation->setCurrentRow(1);
        whatsNew->click();
        QCOMPARE(stack->currentIndex(), 7);
        QCOMPARE(title->text(), QStringLiteral("What's New"));
        QVERIFY(back->isVisible());
        QVERIFY(!navigation->currentItem());

        back->click();
        QCOMPARE(stack->currentIndex(), 1);
        QCOMPARE(navigation->currentRow(), 1);
        QCOMPARE(title->text(), QStringLiteral("General"));
        QVERIFY(!back->isVisible());
    }

    void deletingACorrectionThroughThePageSetKeepsUndoAvailable()
    {
        ApplicationController controller(true);
        const QList<LearnedCorrection> corrections{
            {QStringLiteral("c-1"), QStringLiteral("speecher"), QStringLiteral("Speecher"),
             QStringLiteral("org.kde.konsole"), 1750000000000, 0.92, true, 3, 1750000900000},
            {QStringLiteral("c-2"), QStringLiteral("kay dee ee"), QStringLiteral("KDE"),
             QStringLiteral("org.mozilla.firefox"), 1749000000000, 0.71, false, 1, 1749000500000},
        };
        controller.settings()->setLearnedCorrections(corrections);
        QWidget parent;
        SettingsPageSet pages(&controller, &parent);
        pages.load();

        auto *table = pages.corrections()->findChild<QTableWidget *>(
            QStringLiteral("learnedCorrections"));
        auto *remove = pages.corrections()->findChild<QPushButton *>(
            QStringLiteral("deleteLearnedCorrections"));
        auto *undo = pages.corrections()->findChild<QPushButton *>(
            QStringLiteral("undoDeleteLearnedCorrections"));
        QVERIFY(table && remove && undo);
        QCOMPARE(table->rowCount(), 2);

        // Deleting announces the change, and SettingsPageSet reloads every
        // page from the draft; that echo must not clear the deletion history.
        table->setCurrentCell(0, 0);
        remove->click();
        QCOMPARE(table->rowCount(), 1);
        QVERIFY(undo->isEnabled());

        undo->click();
        QCOMPARE(table->rowCount(), 2);
        AppSettings draft;
        pages.corrections()->appendToDraft(draft);
        QCOMPARE(draft.learnedCorrections, corrections);
    }

    void updateRowCaptionFollowsTheUpdateState()
    {
        // The caption fix hangs on setButtonRowCaption finding the child
        // label by the "rowTitle" object name; a rename would turn it back
        // into a silent no-op with nothing failing.
        ApplicationController controller(true);
        QWidget parent;
        SettingsPageSet pages(&controller, &parent);
        pages.load();

        auto *check = pages.general()->findChild<QPushButton *>(
            QStringLiteral("checkForUpdates"));
        QVERIFY(check);
        auto *title = check->findChild<QLabel *>(QStringLiteral("rowTitle"));
        QVERIFY(title);
        QCOMPARE(title->text(), QStringLiteral("Check now"));

        // The manifest updaters enter Checking synchronously, so the caption
        // can be asserted before the network reply lands. Sparkle hands the
        // check to its own async machinery and may never reach Checking under
        // offscreen tests, so macOS pins only the initial caption lookup.
#ifndef Q_OS_MACOS
        controller.updates()->checkForUpdates(controller.settings()->updateChannel());
        QCOMPARE(title->text(), QStringLiteral("Checking…"));
        QVERIFY(!check->isEnabled());
#endif
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
