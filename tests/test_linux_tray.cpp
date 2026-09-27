#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "frontend/qt/LinuxTrayIcon.h"
#include "frontend/qt/QtFrontEnd.h"
#include "dictation/DictationSession.h"

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QMenu>
#include <QSignalSpy>
#include <QSystemTrayIcon>

using namespace speecher;

class LinuxTrayTests : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        SettingsStore settings;
        settings.raw().clear();
    }

    void menuOffersDictationSettingsAndQuit()
    {
        ApplicationController controller(true);
        LinuxTrayIcon tray(&controller);

        auto *icon = tray.findChild<QSystemTrayIcon *>();
        QVERIFY(icon);
        QCOMPARE(icon->toolTip(), QStringLiteral("Speecher"));

        QMenu *menu = icon->contextMenu();
        QVERIFY(menu);
        const QList<QAction *> actions = menu->actions();
        QCOMPARE(actions.size(), 4);
        QCOMPARE(actions.at(0)->objectName(), QStringLiteral("trayToggleDictation"));
        QCOMPARE(actions.at(0)->text(), QStringLiteral("Start Dictation"));
        QCOMPARE(actions.at(1)->objectName(), QStringLiteral("traySettings"));
        QCOMPARE(actions.at(1)->text(), QStringLiteral("Settings..."));
        QVERIFY(actions.at(2)->isSeparator());
        QCOMPARE(actions.at(3)->objectName(), QStringLiteral("trayQuit"));
        QCOMPARE(actions.at(3)->text(), QStringLiteral("Quit"));
    }

    void listeningStateFlipsToggleTextAndTooltip()
    {
        ApplicationController controller(true);
        LinuxTrayIcon tray(&controller);
        auto *icon = tray.findChild<QSystemTrayIcon *>();
        QAction *toggle = icon->contextMenu()->actions().first();

        emit controller.stateChanged(QStringLiteral("listening"));
        QCOMPARE(toggle->text(), QStringLiteral("Stop Dictation"));
        QCOMPARE(icon->toolTip(), QStringLiteral("Speecher is listening"));

        emit controller.stateChanged(QStringLiteral("starting"));
        QCOMPARE(toggle->text(), QStringLiteral("Stop Dictation"));

        // Toggling mid-refinement cancels the refinement, so the action must
        // not promise a start; during stopping/delivering it does nothing.
        emit controller.stateChanged(QStringLiteral("refining"));
        QCOMPARE(toggle->text(), QStringLiteral("Stop Dictation"));
        QVERIFY(toggle->isEnabled());

        emit controller.stateChanged(QStringLiteral("stopping"));
        QVERIFY(!toggle->isEnabled());

        emit controller.stateChanged(QStringLiteral("delivering"));
        QVERIFY(!toggle->isEnabled());

        emit controller.stateChanged(QStringLiteral("idle"));
        QCOMPARE(toggle->text(), QStringLiteral("Start Dictation"));
        QVERIFY(toggle->isEnabled());
        QCOMPARE(icon->toolTip(), QStringLiteral("Speecher"));
    }

    void quitActionRequestsApplicationQuit()
    {
        ApplicationController controller(true);
        LinuxTrayIcon tray(&controller);
        auto *icon = tray.findChild<QSystemTrayIcon *>();

        QSignalSpy quitRequests(&controller, &ApplicationController::quitRequested);
        icon->contextMenu()->actions().last()->trigger();
        QCOMPARE(quitRequests.count(), 1);
    }

    void aModelReadyNoticeOnlyGoesToTheTrayWhileNoWindowIsShown()
    {
        ApplicationController controller(true);
        QtFrontEnd frontEnd(&controller);
        auto *tray = frontEnd.findChild<LinuxTrayIcon *>();
        QVERIFY(tray);
        QSignalSpy shown(tray, &LinuxTrayIcon::messageShown);

        frontEnd.notifyIfNoWindowShown(QStringLiteral("Parakeet 0.6B is ready"), QStringLiteral("Dictate."),
                                       QStringLiteral("output"));
        QCOMPARE(shown.size(), 1);
        QCOMPARE(shown.first().first().toString(), QStringLiteral("Parakeet 0.6B is ready"));

        // Clicking it opens the page it is about.
        emit tray->messageClicked();
        QWidget *window = nullptr;
        for (QWidget *candidate : QApplication::topLevelWidgets()) {
            if (candidate->objectName() == QStringLiteral("appWindow") && candidate->isVisible()) {
                window = candidate;
            }
        }
        QVERIFY(window);
        QCOMPARE(window->findChild<QLabel *>(QStringLiteral("pageTitle"))->text(), QStringLiteral("Output"));
        window->hide();

        // An error that follows has nothing to open; clicking it must not
        // replay the earlier balloon's action.
        emit controller.session()->popupErrorRequested(QStringLiteral("The microphone stopped."));
        emit tray->messageClicked();
        QVERIFY(!window->isVisible());

        // With the window up, the Local models page already says so.
        frontEnd.showMainWindow();
        QCoreApplication::processEvents();
        frontEnd.notifyIfNoWindowShown(QStringLiteral("Parakeet 0.6B is ready"), QStringLiteral("Dictate."),
                                       QStringLiteral("output"));
        QCOMPARE(shown.size(), 2);
    }
};

int runLinuxTrayTests(int argc, char **argv)
{
    LinuxTrayTests suite;
    return runTestSuite(&suite, argc, argv);
}

#include "test_linux_tray.moc"
