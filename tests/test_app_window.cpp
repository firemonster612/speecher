#include "app/LocalSetup.h"
#include "common/test_suites.h"
#include "common/test_doubles.h"

#include "app/ApplicationController.h"
#include "app/UpdateController.h"
#include "core/InsightsLog.h"
#include "core/SettingsStore.h"
#include "core/SecretStore.h"
#include "core/settings/SettingsKeys.h"
#include "dictation/DictationSession.h"
#include "core/TranscriptState.h"
#include "frontend/qt/QtFrontEnd.h"
#include "frontend/qt/SchemaSettingsPage.h"
#include "ui/AppWindow.h"
#include "ui/InlineMessage.h"
#include "ui/HomePage.h"
#include "ui/InsightsCharts.h"
#include "ui/settings/SettingsPageSet.h"
#include "ui/settings/SettingsPageSupport.h"
#include "transcribe/FileTranscriptionSession.h"
#include "ui/Theme.h"
#include "ui/TranscribeModel.h"
#include "ui/TranscribePage.h"
#include "ui/TranscribeWindow.h"
#ifdef Q_OS_LINUX
#include "ui/setup/LinuxGlobalShortcutSetupPage.h"
#endif

#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QMouseEvent>
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
#include <QMenu>
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
        for (const QString &pane : window.sidebarPanes()) {
            window.showPage(pane);
            QTest::qWait(120);
            window.grab().save(QStringLiteral("%1/%2.png").arg(dir, pane));
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

    // The sidebar is the schema's runs, a gap between each, and nothing else.
    // The sidebar is the schema's groups, each titled one under its header,
    // and nothing else.
    void sidebarListsTheSchemaPanesInTheirGroups()
    {
        ApplicationController controller(true);
        AppWindow window(&controller);
        auto *navigation = window.findChild<QListWidget *>(QStringLiteral("appNavigation"));
        QStringList rows;
        for (int row = 0; row < navigation->count(); ++row) {
            QListWidgetItem *item = navigation->item(row);
            // A header is a bold title and a style-drawn line on an item
            // nothing can pick.
            if (auto *header = navigation->itemWidget(item)) {
                QCOMPARE(header->objectName(), QStringLiteral("sidebarHeader"));
                auto *title = header->findChild<QLabel *>(QStringLiteral("sidebarHeaderTitle"));
                auto *line = header->findChild<QFrame *>(QStringLiteral("sidebarHeaderLine"));
                QVERIFY(title && title->font().bold());
                QVERIFY(line && line->frameShape() == QFrame::HLine);
                QCOMPARE(item->flags(), Qt::NoItemFlags);
                rows.append(QStringLiteral("[%1]").arg(title->text()));
            } else {
                rows.append(item->text());
            }
        }
        QStringList expected{QStringLiteral("Home"), QStringLiteral("General"), QStringLiteral("Accounts"),
                             QStringLiteral("[Speech]"), QStringLiteral("Dictation")};
#ifdef SPEECHER_WITH_LOCAL_SPEECH
        expected.append(QStringLiteral("Local models"));
#endif
        expected += QStringList{QStringLiteral("Transcribe"), QStringLiteral("[Text]"),
                                QStringLiteral("Refinement"), QStringLiteral("Vocabulary"),
                                QStringLiteral("Output")};
        QCOMPARE(rows, expected);

        // A header's title starts where the items' icons do, as System Settings'
        // section titles do, and its line runs to the row's right edge.
        window.show();
        QCoreApplication::processEvents();
        QStyleOptionViewItem option;
        option.initFrom(navigation);
        option.features = QStyleOptionViewItem::HasDisplay | QStyleOptionViewItem::HasDecoration;
        option.decorationSize = navigation->iconSize();
        option.decorationPosition = QStyleOptionViewItem::Left;
        option.rect = navigation->visualItemRect(navigation->item(0));
        const int iconLeft =
            navigation->style()->subElementRect(QStyle::SE_ItemViewItemDecoration, &option, navigation).left();
        for (int row = 0; row < navigation->count(); ++row) {
            if (QWidget *header = navigation->itemWidget(navigation->item(row))) {
                auto *title = header->findChild<QLabel *>(QStringLiteral("sidebarHeaderTitle"));
                auto *line = header->findChild<QFrame *>(QStringLiteral("sidebarHeaderLine"));
                QCOMPARE(title->mapTo(navigation->viewport(), QPoint()).x(), iconLeft);
                QCOMPARE(line->mapTo(navigation->viewport(), QPoint(line->width() - 1, 0)).x(),
                         navigation->visualItemRect(navigation->item(row)).right());
            }
        }

        // Up and Down step over the headers, from one pane to the next.
        navigation->setFocus();
        QTest::keyClick(navigation, Qt::Key_Down);
        QCOMPARE(navigation->currentItem()->text(), QStringLiteral("General"));
        QTest::keyClick(navigation, Qt::Key_Down);
        QTest::keyClick(navigation, Qt::Key_Down);
        QCOMPARE(navigation->currentItem()->text(), QStringLiteral("Dictation"));
        QTest::keyClick(navigation, Qt::Key_Up);
        QCOMPARE(navigation->currentItem()->text(), QStringLiteral("Accounts"));
        QCOMPARE(window.findChild<QLabel *>(QStringLiteral("pageTitle"))->text(), QStringLiteral("Accounts"));
        window.hide();
    }

    void pageIdsOpenPanesAndViewsAndUnknownIdsOpenHome()
    {
        ApplicationController controller(true);
        AppWindow window(&controller);
        auto *navigation = window.findChild<QListWidget *>(QStringLiteral("appNavigation"));
        auto *title = window.findChild<QLabel *>(QStringLiteral("pageTitle"));
        window.showPage(QStringLiteral("vocabulary:corrections"));
        QCOMPARE(navigation->currentItem()->text(), QStringLiteral("Vocabulary"));
        QTabWidget *tabs = nullptr;
        for (QTabWidget *candidate : window.findChildren<QTabWidget *>()) {
            if (candidate->isVisibleTo(&window) || candidate->tabText(0) == QStringLiteral("Vocabulary")) {
                tabs = candidate;
            }
        }
        QVERIFY(tabs);
        QCOMPARE(tabs->tabText(tabs->currentIndex()), QStringLiteral("Learned corrections"));

        QTest::ignoreMessage(QtWarningMsg, "no settings page nosuchpage - showing Home");
        window.showPage(QStringLiteral("nosuchpage"));
        QCOMPARE(navigation->currentItem()->text(), QStringLiteral("Home"));
        QCOMPARE(title->text(), QStringLiteral("Home"));
    }

    // Opened from hidden the window shows Home; brought forward while up it
    // keeps its page.
    void theWindowOpensOnHome()
    {
        ApplicationController controller(true);
        controller.settings()->setSetupCompleted(true);
        QtFrontEnd frontEnd(&controller);
        controller.setFrontEnd(&frontEnd);
        const auto title = [] {
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                if (qobject_cast<AppWindow *>(widget) && widget->isVisible()) {
                    return widget->findChild<QLabel *>(QStringLiteral("pageTitle"))->text();
                }
            }
            return QString();
        };
        controller.showMainWindow();
        QCOMPARE(title(), QStringLiteral("Home"));
        controller.showSettingsWindow();
        QCOMPARE(title(), QStringLiteral("General"));
        controller.showMainWindow();
        QCOMPARE(title(), QStringLiteral("General"));
        frontEnd.hideMainWindow();
        controller.showMainWindow();
        QCOMPARE(title(), QStringLiteral("Home"));
        frontEnd.hideMainWindow();
    }

    // Picking What's New in the sidebar is the same as opening it any other
    // way: Back returns to where it was opened from, and it is no longer
    // pending.
    void whatsNewPickedInTheSidebarReturnsWhereItCameFrom()
    {
        SettingsStore().setUpdatesPendingWhatsNewVersion(QStringLiteral("0.0.1"));
        ApplicationController controller(true);
        QVERIFY(!controller.pendingWhatsNewVersion().isEmpty());
        AppWindow window(&controller);
        auto *navigation = window.findChild<QListWidget *>(QStringLiteral("appNavigation"));
        auto *title = window.findChild<QLabel *>(QStringLiteral("pageTitle"));
        window.showPage(QStringLiteral("output"));
        QCOMPARE(navigation->item(0)->text(), QStringLiteral("What's New"));

        navigation->setCurrentRow(0);
        QCOMPARE(title->text(), QStringLiteral("What's New"));
        QVERIFY(controller.pendingWhatsNewVersion().isEmpty());

        window.findChild<QToolButton *>(QStringLiteral("whatsNewBack"))->click();
        QCOMPARE(title->text(), QStringLiteral("Output"));
        QCOMPARE(navigation->currentItem()->text(), QStringLiteral("Output"));
        QCOMPARE(navigation->item(0)->text(), QStringLiteral("Home"));
    }

    void settingsOpensGeneral()
    {
        ApplicationController controller(true);
        controller.settings()->setSetupCompleted(true);
        QtFrontEnd frontEnd(&controller);
        controller.setFrontEnd(&frontEnd);
        controller.showSettingsWindow();
        const auto windows = QApplication::topLevelWidgets();
        const auto window = std::find_if(windows.cbegin(), windows.cend(), [](QWidget *widget) {
            return qobject_cast<AppWindow *>(widget) && widget->isVisible();
        });
        QVERIFY(window != windows.cend());
        QCOMPARE((*window)->findChild<QLabel *>(QStringLiteral("pageTitle"))->text(),
                 QStringLiteral("General"));
        (*window)->hide();
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
        QSignalSpy navigate(&page, &HomePage::pageRequested);
        page.findChild<QPushButton *>(QStringLiteral("insightsNoticeSettings"))->click();
        QCOMPARE(navigate.count(), 1);
        QCOMPARE(navigate.first().first().toString(), QStringLiteral("general"));

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
        const QList<QAction *> share =
            page.findChild<QToolButton *>(QStringLiteral("shareInsights"))->menu()->actions();
        share.at(0)->trigger();
        QVERIFY(!QGuiApplication::clipboard()->image().isNull());
        share.at(1)->trigger();
        QVERIFY(QGuiApplication::clipboard()->text().startsWith(
            QStringLiteral("My Speecher stats, last 30 days\n90 words dictated")));

        // Clearing the log leaves "No insights yet".
        controller.clearInsights();
        QVERIFY(notice->isVisible());
        QCOMPARE(notice->findChild<QLabel *>(QStringLiteral("insightsNoticeTitle"))->text(),
                 QStringLiteral("No insights yet"));
        QVERIFY(page.findChildren<QFrame *>(QStringLiteral("insightTile")).isEmpty());
    }

    void homeTilesGoFourAcrossOnlyWhenNoLineWraps_data()
    {
        // 0 keeps the platform's font; at 7 px the tiles' title rows and
        // lines differ most in width, so a tile squeezed by its neighbours
        // would wrap.
        QTest::addColumn<int>("pixelSize");
        QTest::newRow("platform font") << 0;
        QTest::newRow("7 px") << 7;
    }

    void homeTilesGoFourAcrossOnlyWhenNoLineWraps()
    {
        QFETCH(int, pixelSize);
        qputenv("SPEECHER_INSIGHTS_SEED",
                QFINDTESTDATA("../docs/insights-mockup/seed-active.jsonl").toLocal8Bit());
        qputenv("SPEECHER_INSIGHTS_TODAY", "2026-09-26");
        const auto restore = qScopeGuard([] {
            qunsetenv("SPEECHER_INSIGHTS_SEED");
            qunsetenv("SPEECHER_INSIGHTS_TODAY");
        });
        const QFont originalFont = QApplication::font();
        if (pixelSize > 0) {
            QFont font = originalFont;
            font.setPixelSize(pixelSize);
            QApplication::setFont(font);
        }
        const auto restoreFont = qScopeGuard([originalFont] { QApplication::setFont(originalFont); });
        ApplicationController controller(true);
        controller.settings()->setInsightsEnabled(true);
        HomePage page(&controller);
        // The page caps Home's column at a width that, with some platforms'
        // fonts, never fits four tiles. Lift the cap so the column follows
        // the page and both layouts come up on every platform.
        delete page.findChild<QObject *>(QStringLiteral("pageWidthGovernor"));
        page.findChild<QScrollArea *>()->widget()->setMaximumWidth(QWIDGETSIZE_MAX);
        page.show();
        const auto wrapped = [&page] {
            for (const QFrame *tile : page.findChildren<QFrame *>(QStringLiteral("insightTile"))) {
                for (const QLabel *line : tile->findChildren<QLabel *>()) {
                    if (line->wordWrap() && line->height() > line->fontMetrics().height() * 3 / 2) {
                        return true;
                    }
                }
            }
            return false;
        };
        const auto fourAcross = [&page] {
            const auto *grid = page.findChild<QWidget *>(QStringLiteral("insightTiles"));
            return qobject_cast<QGridLayout *>(grid->layout())->itemAtPosition(0, 3) != nullptr;
        };
        const auto layOut = [&page](int width) {
            page.resize(width, 800);
            QCoreApplication::processEvents();
            QCoreApplication::processEvents();
        };
        // The coarse pass finds where four across starts; a line that wraps
        // does so just past that point, so the pixels around it are all tried.
        int firstFour = 0;
        for (int width = 300; width <= 2400 && !firstFour; width += 20) {
            layOut(width);
            if (fourAcross()) firstFour = width;
        }
        QVERIFY2(firstFour > 300, "the tiles should start two by two and go four across");
        // A tile that cannot shrink widens the page instead of wrapping, so
        // the column staying inside the page is part of fitting.
        auto *column = page.findChild<QScrollArea *>()->widget();
        for (int width = firstFour - 20; width <= firstFour + 20; ++width) {
            layOut(width);
            if (!fourAcross()) continue;
            QVERIFY2(!wrapped(), qPrintable(QStringLiteral("a line wraps four across at %1").arg(width)));
            QVERIFY2(column->width() <= page.width(),
                     qPrintable(QStringLiteral("four across overflows the page at %1").arg(width)));
        }
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
        // Hovering a heatmap day or an hour bar shows its tip on the move
        // itself, not after the platform's tooltip delay. The moves go to the
        // chart directly: a synthetic cursor would reach whatever window an
        // earlier test left under it.
        const auto hover = [](QWidget *widget, QPoint at) {
            QMouseEvent move(QEvent::MouseMove, QPointF(at), widget->mapToGlobal(QPointF(at)),
                             Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(widget, &move);
        };
        const QDate today(2026, 9, 26);
        QList<HeatmapDay> days;
        for (int offset = 6; offset >= 0; --offset) {
            days.append({today.addDays(-offset), offset == 0 ? 3 : 1, 30, 60000});
        }
        // The week strip has no tips, as on macOS and Windows.
        InsightsHeatmap week(InsightsHeatmap::Shape::Week);
        week.setDays(days);
        week.resize(week.sizeHint());
        week.show();
        QVERIFY(QTest::qWaitForWindowExposed(&week));
        hover(&week, QPoint(week.width() / 14, 6));
        QTest::qWait(50);
        QVERIFY(!QToolTip::isVisible());

        InsightsBarChart hours;
        std::array<int, 24> counts{};
        counts[10] = 4;
        hours.setCounts(counts, 10);
        hours.resize(480, hours.sizeHint().height());
        hours.show();
        QVERIFY(QTest::qWaitForWindowExposed(&hours));
        hover(&hours, QPoint(480 * 10 / 24 + 5, 10));
        QTRY_VERIFY_WITH_TIMEOUT(QToolTip::isVisible(), 200);
        QVERIFY(QToolTip::text().contains(QStringLiteral("4 dictations")));
        QToolTip::hideText();
    }

    void chartTipStaysWhenThePointerStopsInAGap()
    {
        // The pointer crosses a heatmap cell and stops in the 3 px gap beside
        // it, which still counts as that cell. Qt hides a tip 300 ms after a
        // move lands outside the area it was shown for, so that area has to
        // take in the gap too, or the tip vanishes as the pointer stops.
        const auto moveTo = [](QWidget *widget, QPoint at) {
            QMouseEvent move(QEvent::MouseMove, QPointF(at), widget->mapToGlobal(QPointF(at)),
                             Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(widget, &move);
        };
        const QDate today(2026, 9, 26);
        QList<HeatmapDay> days;
        for (int offset = 20; offset >= 0; --offset) {
            days.append({today.addDays(-offset), 1, 30, 60000});
        }
        InsightsHeatmap year(InsightsHeatmap::Shape::Year);
        year.setDays(days);
        year.resize(600, year.heightForWidth(600));
        year.show();
        // Qt closes any open tooltip when a window activates, and a compositor
        // may activate this one only after it is exposed.
        year.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&year));
        // From the rendered chart: the newest week's first cell (the topmost
        // cell pixel at the right) and the first background pixel below it,
        // which is in the gap just past the cell's edge.
        const QImage pixels = year.grab().toImage();
        const QColor background = pixels.pixelColor(0, pixels.height() - 1);
        QPoint cellTop(-1, -1);
        for (int y = 0; y < pixels.height() && cellTop.x() < 0; ++y) {
            for (int x = pixels.width() - 1; x > pixels.width() / 2; --x) {
                if (pixels.pixelColor(x, y) != background) {
                    cellTop = QPoint(x - 3, y);
                    break;
                }
            }
        }
        QVERIFY2(cellTop.x() > 0, "the heatmap drew no cells");
        QPoint gap;
        for (int y = cellTop.y(); y < pixels.height(); ++y) {
            if (pixels.pixelColor(cellTop.x(), y) == background) {
                gap = QPoint(cellTop.x(), y);
                break;
            }
        }
        QVERIFY2(!gap.isNull(), "found no gap below the newest cell");

        moveTo(&year, cellTop + QPoint(0, 2));
        QTRY_VERIFY_WITH_TIMEOUT(QToolTip::isVisible(), 200);
        moveTo(&year, gap);
        QTest::qWait(500);
        QVERIFY2(QToolTip::isVisible(), "the tip closed with the pointer stopped in the gap");
        QToolTip::hideText();
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
            startControls += button->text() == QStringLiteral("Start dictation");
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

        // An unknown speech provider fails the session as it starts.
        controller.settings()->setSpeechProvider(QStringLiteral("missing"));
        controller.session()->startListening();
        QCOMPARE(controller.session()->stateName(), QStringLiteral("error"));
        QVERIFY(!controller.session()->lastFailure().isEmpty());
        // The status line says it, so the note under it waits.
        auto *status = page.findChild<QLabel *>(QStringLiteral("dictationStatus"));
        QVERIFY(status);
        QCOMPARE(status->text().simplified(), controller.session()->lastFailure());
        QVERIFY(!error->isVisible());

        // Leaving the error state shows it; only a new session hides it again.
        controller.session()->stopListening();
        QCOMPARE(controller.session()->stateName(), QStringLiteral("idle"));
        QVERIFY(error->isVisible());
        QCOMPARE(error->text(), controller.session()->lastFailure());
        controller.settings()->setSpeechProvider(QStringLiteral("claude"));
        controller.session()->startListening();
        QVERIFY(controller.session()->lastFailure().isEmpty());
        QVERIFY(!error->isVisible());
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

    // The status line says what is happening; the button says what a click
    // does, which is nothing while a stop or delivery is under way.
    void homeShowsHonestStatusAndActions()
    {
        ApplicationController controller(true);
        HomePage page(&controller);
        auto *status = page.findChild<QLabel *>(QStringLiteral("dictationStatus"));
        QVERIFY(status);

        page.setStatus(QStringLiteral("idle"));
        QCOMPARE(status->text(), QStringLiteral("Idle"));
        QCOMPARE(page.toggleButton()->text(), QStringLiteral("Start dictation"));

        page.setStatus(QStringLiteral("listening"));
        QCOMPARE(status->text(), QStringLiteral("Listening…"));
        QCOMPARE(page.toggleButton()->text(), QStringLiteral("Stop dictation"));

        page.setStatus(QStringLiteral("refining"));
        QCOMPARE(status->text(), QStringLiteral("Refining…"));
        QCOMPARE(page.toggleButton()->text(), QStringLiteral("Cancel refinement"));
        QVERIFY(page.toggleButton()->isEnabled());

        page.setStatus(QStringLiteral("stopping"));
        QCOMPARE(status->text(), QStringLiteral("Transcribing…"));
        QVERIFY(!page.toggleButton()->isEnabled());

        page.setStatus(QStringLiteral("delivering"));
        QVERIFY(!page.toggleButton()->isEnabled());

        // A receipt or an error reaches the line as the session words it.
        emit controller.statusChanged(QStringLiteral("Input sent"));
        QCOMPARE(status->text(), QStringLiteral("Input sent"));
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

        auto *autoCheck = pages.page(QStringLiteral("whatsNew"))->findChild<QCheckBox *>(
            QStringLiteral("autoCheckUpdates"));
        QVERIFY(autoCheck);
        autoCheck->setChecked(false);
        QVERIFY(pages.save(false, false));

        auto *channel = pages.page(QStringLiteral("general"))->findChild<QComboBox *>(QStringLiteral("updateChannel"));
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
        QCOMPARE(navigation->count(), 1);
        QCOMPARE(navigation->item(0)->text(), QStringLiteral("Dictation"));
        // Hits across several groups come without headers.
        search->setText(QStringLiteral("e"));
        QVERIFY(navigation->count() > 3);
        for (int row = 0; row < navigation->count(); ++row) {
            QVERIFY(!navigation->itemWidget(navigation->item(row)));
            QVERIFY(!navigation->item(row)->text().isEmpty());
        }
        // Return opens the first hit.
        search->setText(QStringLiteral("paste"));
        QCOMPARE(navigation->item(0)->text(), QStringLiteral("Output"));
        QTest::keyClick(search, Qt::Key_Return);
        QCOMPARE(window.findChild<QLabel *>(QStringLiteral("pageTitle"))->text(), QStringLiteral("Output"));
        // What's New is not searchable, even for its own name: General
        // answers, for the row that opens it.
        search->setText(QStringLiteral("What's New"));
        QCOMPARE(navigation->count(), 1);
        QCOMPARE(navigation->item(0)->text(), QStringLiteral("General"));
    }

    void openedAudioFilesLandOnTheTranscribePage()
    {
        ApplicationController controller(true);
        AppWindow window(&controller);
        QTemporaryDir dir;
        const QString audio = dir.filePath(QStringLiteral("memo.wav"));
        QFile file(audio);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("RIFF\0\0\0\0WAVEfmt ");
        file.close();

        window.showTranscribeFiles({audio, dir.filePath(QStringLiteral("missing.wav"))});

        auto *navigation = window.findChild<QListWidget *>(QStringLiteral("appNavigation"));
        QCOMPARE(navigation->currentItem()->text(), QStringLiteral("Transcribe"));
        auto *start = window.findChild<QPushButton *>(QStringLiteral("transcribeStart"));
        QVERIFY(start->isEnabled());
        QCOMPARE(start->text(), QStringLiteral("Transcribe"));
        QVERIFY(!controller.fileTranscription()->isRunning());
    }

    void openedFilesGetTheCompactWindowAlone()
    {
        ApplicationController controller(true);
        controller.settings()->setSetupCompleted(true);
        QtFrontEnd frontEnd(&controller);
        controller.setFrontEnd(&frontEnd);
        const QString audio = writeHeaderOnlyWav();
        const QSet<QWidget *> before = visibleWindows();
        controller.showTranscribeFiles({audio});
        controller.showTranscribeFiles({audio});

        const QWidgetList windows = (visibleWindows() - before).values();
        QCOMPARE(windows.size(), 1);
        QCOMPARE(windows.first()->objectName(), QStringLiteral("transcribeWindow"));
        QCOMPARE(windows.first()->windowTitle(), QStringLiteral("Transcribe \u2014 Speecher"));
        QVERIFY(!windows.first()->findChild<QListWidget *>(QStringLiteral("appNavigation")));
        auto *start = windows.first()->findChild<QPushButton *>(QStringLiteral("transcribeStart"));
        QVERIFY(start->isEnabled());
        QCOMPARE(start->text(), QStringLiteral("Transcribe"));
    }

    // Files opened before setup was done wait for it, then open alone: the
    // assistant finishing does not bring the main window up beside them.
    void filesHeldThroughSetupGetTheCompactWindowAlone()
    {
        ApplicationController controller(false);
        controller.settings()->setSetupCompleted(false);
        const auto restore = qScopeGuard([&] { controller.settings()->setSetupCompleted(true); });
        QtFrontEnd frontEnd(&controller);
        controller.setFrontEnd(&frontEnd);
        const QString audio = writeHeaderOnlyWav();
        const QSet<QWidget *> before = visibleWindows();

        controller.showTranscribeFiles({audio});
        const QWidgetList opened = (visibleWindows() - before).values();
        QCOMPARE(opened.size(), 1);
        auto *assistant = qobject_cast<QDialog *>(opened.first());
        QVERIFY(assistant);
        // What the assistant's Finish does, without walking its pages.
        controller.completeSetup();
        assistant->done(QDialog::Accepted);

        QTRY_VERIFY(!(visibleWindows() - before).contains(assistant));
        QTRY_COMPARE((visibleWindows() - before).size(), 1);
        QCOMPARE((visibleWindows() - before).values().first()->objectName(), QStringLiteral("transcribeWindow"));
    }

    // Files that arrive just after a plain launch put the main window up
    // take its place, however late the platform delivers them.
    void filesOpenedAtLaunchReplaceTheDefaultMainWindow()
    {
        ApplicationController controller(false);
        controller.settings()->setSetupCompleted(true);
        QtFrontEnd frontEnd(&controller);
        controller.setFrontEnd(&frontEnd);
        const QString audio = writeHeaderOnlyWav();
        const QSet<QWidget *> before = visibleWindows();

        controller.showDefaultMainWindow();
        const QWidgetList shown = (visibleWindows() - before).values();
        QCOMPARE(shown.size(), 1);
        QWidget *main = shown.first();
        controller.showTranscribeFiles({audio});
        QVERIFY(!main->isVisible());

        // Asked for by name, the main window stays when more files arrive.
        controller.showMainWindow();
        controller.showTranscribeFiles({audio});
        QVERIFY(main->isVisible());
        main->hide();
    }

    void aFileOpenedOverResultsIsKeptForTheNextBatch()
    {
        ApplicationController controller(true);
        AppWindow window(&controller);
        QTemporaryDir dir;
        const auto writeHeaderOnlyWav = [&dir](const QString &name) {
            QFile file(dir.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("RIFF\0\0\0\0WAVEfmt ");
        };
        writeHeaderOnlyWav(QStringLiteral("first.wav"));
        writeHeaderOnlyWav(QStringLiteral("second.wav"));
        window.showTranscribeFiles({dir.filePath(QStringLiteral("first.wav"))});

        // A header with no audio fails to decode, which still ends on results.
        window.findChild<QPushButton *>(QStringLiteral("transcribeStart"))->click();
        auto *again = window.findChild<QPushButton *>(QStringLiteral("transcribeAgain"));
        QTRY_VERIFY_WITH_TIMEOUT(again->isVisibleTo(&window), 10000);
        QVERIFY(window.findChild<QToolButton *>(QStringLiteral("transcribeRetry")));

        window.showTranscribeFiles({dir.filePath(QStringLiteral("second.wav"))});

        auto *start = window.findChild<QPushButton *>(QStringLiteral("transcribeStart"));
        QVERIFY(start->isVisibleTo(&window));
        QVERIFY(start->isEnabled());
        QStringList listed;
        for (const QLabel *label : window.findChildren<QLabel *>()) {
            if (label->text().endsWith(QStringLiteral(".wav")) && label->isVisibleTo(&window)) {
                listed << label->text();
            }
        }
        QCOMPARE(listed, QStringList({QStringLiteral("second.wav")}));
    }

    // The main window's page and the compact window show one batch: files
    // listed in either appear in both, a batch started in one runs in the
    // other, and its results reach both.
    void bothTranscribeViewsShowTheSameBatch()
    {
        ApplicationController controller(true);
        TranscribePage page(&controller);
        TranscribeWindow window(&controller);
        const QString audio = writeHeaderOnlyWav();
        const auto listed = [audio](QWidget *view) {
            for (const QLabel *label : view->findChildren<QLabel *>()) {
                if (label->text() == QFileInfo(audio).fileName() && label->isVisibleTo(view)) {
                    return true;
                }
            }
            return false;
        };

        window.page()->addFiles({audio});
        QVERIFY(listed(&page));
        auto *pageStart = page.findChild<QPushButton *>(QStringLiteral("transcribeStart"));
        QVERIFY(pageStart->isEnabled());

        // A header with no audio fails to decode, which still ends on results.
        pageStart->click();
        auto *windowAgain = window.findChild<QPushButton *>(QStringLiteral("transcribeAgain"));
        QTRY_VERIFY_WITH_TIMEOUT(windowAgain->isVisibleTo(&window), 10000);
        QVERIFY(page.findChild<QPushButton *>(QStringLiteral("transcribeAgain"))->isVisibleTo(&page));
        QVERIFY(window.findChild<QToolButton *>(QStringLiteral("transcribeRetry")));

        windowAgain->click();
        QVERIFY(pageStart->isVisibleTo(&page));
        QVERIFY(!listed(&page));
    }

    // Going back to setup while a retry runs, then starting the next batch:
    // the retry stops, the batch starts, and its results are the new file's.
    // A start refused while something runs changes nothing.
    void aRetryLeftForTheNextBatchStopsAndLeavesItAlone()
    {
        ApplicationController controller(true);
        TranscribeModel *model = TranscribeModel::of(&controller);
        const QString first = writeHeaderOnlyWav();
        const QString second = m_files.filePath(QStringLiteral("second.wav"));
        QVERIFY(QFile::copy(first, second));
        model->addFiles({first});
        QString error;
        // A header with no audio fails to decode, which still ends on results.
        QVERIFY(model->start({}, &error));
        QTRY_COMPARE_WITH_TIMEOUT(model->step(), TranscribeStep::Export, 10000);

        // Real audio this time, so the retry decodes over later event-loop
        // turns and is still running for the checks below.
        {
            QFile audio(first);
            QVERIFY(audio.open(QIODevice::WriteOnly));
            // One second of 16 kHz mono silence.
            audio.write(test::wavBytes(QByteArray(16000 * 2, '\0'), 16000, 1));
        }
        model->retry(0);
        QCOMPARE(model->retrying(), 0);
        QVERIFY(controller.fileTranscription()->isRunning());
        QVERIFY(!model->start({}, &error));
        QCOMPARE(model->step(), TranscribeStep::Export);
        QCOMPARE(model->results().size(), 1);
        QCOMPARE(model->batch(), QStringList({first}));

        model->backToSetup();
        QCOMPARE(model->retrying(), -1);
        QVERIFY(!controller.fileTranscription()->isRunning());
        model->addFiles({second});
        QVERIFY2(model->start({}, &error), qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(model->step(), TranscribeStep::Export, 10000);
        QTest::qWait(100);

        QCOMPARE(model->batch(), QStringList({second}));
        QCOMPARE(model->results().size(), 1);
        QCOMPARE(model->results().first().path, second);
        QCOMPARE(model->retrying(), -1);
    }

    // An export that fails says why beside the results; the batch summary stays.
    void anExportProblemLeavesTheSummary()
    {
        ApplicationController controller(true);
        TranscribePage page(&controller);
        page.addFiles({writeHeaderOnlyWav()});
        page.findChild<QPushButton *>(QStringLiteral("transcribeStart"))->click();
        auto *summary = page.findChild<QLabel *>(QStringLiteral("transcribeSummary"));
        QTRY_VERIFY_WITH_TIMEOUT(summary->isVisibleTo(&page), 10000);
        const QString before = summary->text();
        QVERIFY(!before.isEmpty());

        TranscribeModel::of(&controller)->setProblem(QStringLiteral("Could not save /nowhere: denied"));

        auto *problem = page.findChild<InlineMessage *>(QStringLiteral("transcribeProblem"));
        QVERIFY(problem->isVisibleTo(&page));
        QCOMPARE(problem->label()->text(), QStringLiteral("Could not save /nowhere: denied"));
        QCOMPARE(summary->text(), before);
        const QString grabDir = qEnvironmentVariable("SPEECHER_TEST_GRAB_DIR");
        if (!grabDir.isEmpty()) {
            page.resize(620, 420);
            page.show();
            QTest::qWait(200);
            page.grab().save(grabDir + QStringLiteral("/export-error.png"));
        }
    }

    void programmaticNavigationUpdatesShellChrome()
    {
        ApplicationController controller(true);
        AppWindow window(&controller);
        window.showPage(QStringLiteral("output"));
        QCOMPARE(window.findChild<QListWidget *>(QStringLiteral("appNavigation"))->currentItem()->text(),
                 QStringLiteral("Output"));
        QCOMPARE(window.findChild<QLabel *>(QStringLiteral("pageTitle"))->text(), QStringLiteral("Output"));
    }

    void whatsNewOffersAWayBackToThePageItWasOpenedFrom()
    {
        ApplicationController controller(true);
        AppWindow window(&controller);
        window.show();
        auto *navigation = window.findChild<QListWidget *>(QStringLiteral("appNavigation"));
        auto *whatsNew = window.findChild<QPushButton *>(QStringLiteral("whatsNew"));
        auto *back = window.findChild<QToolButton *>(QStringLiteral("whatsNewBack"));
        auto *title = window.findChild<QLabel *>(QStringLiteral("pageTitle"));
        QVERIFY(navigation && whatsNew && back && title);
        QVERIFY(!back->isVisible());

        // Opened from General, the same way the update banner opens it. It
        // sits at the top of the sidebar while it shows.
        window.showPage(QStringLiteral("general"));
        whatsNew->click();
        QCOMPARE(title->text(), QStringLiteral("What's New"));
        QVERIFY(back->isVisible());
        QCOMPARE(navigation->item(0)->text(), QStringLiteral("What's New"));
        QCOMPARE(navigation->item(1)->text(), QStringLiteral("Home"));
        QCOMPARE(navigation->currentItem(), navigation->item(0));

        back->click();
        QCOMPARE(navigation->currentItem()->text(), QStringLiteral("General"));
        QCOMPARE(title->text(), QStringLiteral("General"));
        QVERIFY(!back->isVisible());
        // Not pending and not showing, so it leaves the sidebar.
        QCOMPARE(navigation->item(0)->text(), QStringLiteral("Home"));
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

        SchemaSettingsPage *correctionsPage = pages.page(QStringLiteral("vocabulary:corrections"));
        auto *table = correctionsPage->findChild<QTableWidget *>(
            QStringLiteral("learnedCorrections"));
        auto *remove = correctionsPage->findChild<QPushButton *>(
            QStringLiteral("deleteLearnedCorrections"));
        auto *undo = correctionsPage->findChild<QPushButton *>(
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
        correctionsPage->appendToDraft(draft);
        QCOMPARE(draft.learnedCorrections, corrections);
    }

    void localRefreshDoesNotReadUnreadKeyringSecrets()
    {
        ApplicationController controller(true);
        controller.settings()->raw().setValue(SettingsKeys::SecretsInKeyring,
                                              QStringList{QStringLiteral("speech-endpoint-key")});
        qputenv("SPEECHER_TEST_KEYRING_READ_TIMEOUT", "1");
        const auto restore = qScopeGuard([] { qunsetenv("SPEECHER_TEST_KEYRING_READ_TIMEOUT"); });
        QWidget parent;
        // Isolate the refresh's settings read from the schema's liveFacts queries.
        SettingsPageSet pages(&controller, &parent, buildSettingsSchema({}));
        QVERIFY(!controller.secretStore()->isSecretKnown(SecretStore::Secret::SpeechEndpointKey));

        emit controller.localSetup()->changed();

        QVERIFY2(controller.secretStore()->lastError().isEmpty(), "Local refresh tried to read the keyring");
        QVERIFY(!controller.secretStore()->isSecretKnown(SecretStore::Secret::SpeechEndpointKey));
    }

    void localRefreshPreservesPendingSettingsEdits()
    {
#ifndef SPEECHER_WITH_LOCAL_SPEECH
        QSKIP("The Local models page needs local speech");
#endif
        ApplicationController controller(true);
        controller.settings()->raw().clear();
        QWidget parent;
        SettingsPageSet pages(&controller, &parent);
        pages.load();
        auto *idle = parent.findChild<QComboBox *>("localIdleUnload");
        QVERIFY(idle);
        idle->setCurrentIndex(idle->findData(60));
        emit controller.localSetup()->changed();
        QVERIFY(pages.save(false, false));
        QCOMPARE(controller.settings()->localSpeechSettings().idleUnloadMinutes, 60);
    }

    void updateRowCaptionFollowsTheUpdateState()
    {
        ApplicationController controller(true);
        QWidget parent;
        SettingsPageSet pages(&controller, &parent);
        pages.load();

        auto *check = pages.page(QStringLiteral("general"))->findChild<QPushButton *>(
            QStringLiteral("checkForUpdates"));
        QVERIFY(check);
        // The row keeps its label; the button says what a click does.
        auto *row = check->parentWidget();
        while (row && !row->findChild<QLabel *>(QStringLiteral("rowTitle"))) {
            row = row->parentWidget();
        }
        QVERIFY(row);
        QCOMPARE(row->findChild<QLabel *>(QStringLiteral("rowTitle"))->text(),
                 QStringLiteral("Check for updates"));
        QCOMPARE(check->text(), QStringLiteral("Check now"));

        // The manifest updaters enter Checking synchronously, so the caption
        // can be asserted before the network reply lands. Sparkle hands the
        // check to its own async machinery and may never reach Checking under
        // offscreen tests, so macOS pins only the initial caption lookup.
#ifndef Q_OS_MACOS
        controller.updates()->checkForUpdates(controller.settings()->updateChannel());
        QCOMPARE(check->text(), QStringLiteral("Checking…"));
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
        pages.page(QStringLiteral("vocabulary:replacements"))->load(withDuplicateBinding);

        QVERIFY(!pages.save(false, true, &outcome));
        QCOMPARE(outcome.failure, SettingsPageSet::SaveFailure::InvalidReplacementRules);
        QCOMPARE(outcome.messages,
                 QStringList{QStringLiteral(
                     "Row 2 duplicates the normalized spoken phrase from row 1.")});
    }

private:
    // Earlier tests can leave windows behind; a test counts only what it opens.
    static QSet<QWidget *> visibleWindows()
    {
        QSet<QWidget *> visible;
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (widget->isVisible()) {
                visible << widget;
            }
        }
        return visible;
    }

    // A WAV header with no audio: an audio file to list, which fails to decode.
    QString writeHeaderOnlyWav()
    {
        const QString path = m_files.filePath(QStringLiteral("memo.wav"));
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write("RIFF\0\0\0\0WAVEfmt ", 16);
        }
        return path;
    }

    QTemporaryDir m_files;
};

int runAppWindowTests(int argc, char **argv)
{
    AppWindowTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_app_window.moc"
