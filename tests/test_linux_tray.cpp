#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "frontend/qt/LinuxTrayIcon.h"
#include "frontend/qt/QtFrontEnd.h"
#include "dictation/DictationSession.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QLabel>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
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
        QCOMPARE(actions.at(0)->text(), QStringLiteral("Start dictation"));
        QCOMPARE(actions.at(1)->objectName(), QStringLiteral("traySettings"));
        QCOMPARE(actions.at(1)->text(), QStringLiteral("Settings…"));
        QVERIFY(actions.at(2)->isSeparator());
        QCOMPARE(actions.at(3)->objectName(), QStringLiteral("trayQuit"));
        QCOMPARE(actions.at(3)->text(), QStringLiteral("Quit Speecher"));
    }

    void listeningStateFlipsToggleTextAndTooltip()
    {
        ApplicationController controller(true);
        LinuxTrayIcon tray(&controller);
        auto *icon = tray.findChild<QSystemTrayIcon *>();
        QAction *toggle = icon->contextMenu()->actions().first();

        emit controller.stateChanged(QStringLiteral("listening"));
        QCOMPARE(toggle->text(), QStringLiteral("Stop dictation"));
        QCOMPARE(icon->toolTip(), QStringLiteral("Speecher is listening"));

        emit controller.stateChanged(QStringLiteral("starting"));
        QCOMPARE(toggle->text(), QStringLiteral("Stop dictation"));

        // Toggling mid-refinement cancels the refinement, so the action says
        // so; during stopping/delivering it does nothing.
        emit controller.stateChanged(QStringLiteral("refining"));
        QCOMPARE(toggle->text(), QStringLiteral("Cancel refinement"));
        QVERIFY(toggle->isEnabled());

        emit controller.stateChanged(QStringLiteral("stopping"));
        QVERIFY(!toggle->isEnabled());

        emit controller.stateChanged(QStringLiteral("delivering"));
        QVERIFY(!toggle->isEnabled());

        emit controller.stateChanged(QStringLiteral("idle"));
        QCOMPARE(toggle->text(), QStringLiteral("Start dictation"));
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
        QWidget *window = visibleAppWindow();
        QVERIFY(window);
        QCOMPARE(window->findChild<QLabel *>(QStringLiteral("pageTitle"))->text(), QStringLiteral("Output"));
        window->hide();

        // With the window up, the Local models page already says so.
        frontEnd.showMainWindow();
        QCoreApplication::processEvents();
        frontEnd.notifyIfNoWindowShown(QStringLiteral("Parakeet 0.6B is ready"), QStringLiteral("Dictate."),
                                       QStringLiteral("output"));
        QCOMPARE(shown.size(), 1);
        window->hide();
    }

    // The popup and Home already show a dictation error, so the tray posts
    // no notification of its own.
    void aDictationErrorPostsNoNotification()
    {
        ApplicationController controller(true);
        QtFrontEnd frontEnd(&controller);
        auto *tray = frontEnd.findChild<LinuxTrayIcon *>();
        QSignalSpy shown(tray, &LinuxTrayIcon::messageShown);
        emit controller.session()->popupErrorRequested(QStringLiteral("The microphone stopped."));
        QVERIFY(shown.isEmpty());
    }

    // Clicking the icon opens the status panel, as on macOS and Windows; its
    // Settings… opens General.
    void clickingTheIconOpensTheStatusPanel()
    {
        ApplicationController controller(true);
        QtFrontEnd frontEnd(&controller);
        controller.setFrontEnd(&frontEnd);
        auto *tray = frontEnd.findChild<LinuxTrayIcon *>();
        auto *icon = tray->findChild<QSystemTrayIcon *>();
        TrayStatusPanel *panel = tray->panel();

        emit icon->activated(QSystemTrayIcon::Trigger);
        QVERIFY(panel->isVisible());
        QVERIFY(!visibleAppWindow());

        panel->findChild<QPushButton *>(QStringLiteral("trayPanelSettings"))->click();
        QVERIFY(!panel->isVisible());
        QWidget *window = visibleAppWindow();
        QVERIFY(window);
        QCOMPARE(window->findChild<QLabel *>(QStringLiteral("pageTitle"))->text(), QStringLiteral("General"));
        frontEnd.hideMainWindow();
    }

    void thePanelFollowsTheSessionAndKeepsTheLastTranscript()
    {
        ApplicationController controller(true);
        TrayStatusPanel panel(&controller);
        auto *heading = panel.findChild<QLabel *>(QStringLiteral("trayPanelHeading"));
        auto *level = panel.findChild<QProgressBar *>(QStringLiteral("trayPanelLevel"));
        auto *toggle = panel.findChild<QPushButton *>(QStringLiteral("trayPanelToggle"));
        auto *transcript = panel.findChild<QLabel *>(QStringLiteral("trayPanelTranscript"));
        auto *copy = panel.findChild<QPushButton *>(QStringLiteral("trayPanelCopy"));
        auto *quit = panel.findChild<QPushButton *>(QStringLiteral("trayPanelQuit"));
        panel.show();

        QCOMPARE(heading->text(), QStringLiteral("Idle"));
        QVERIFY(!level->isVisible());
        QCOMPARE(toggle->text(), QStringLiteral("Start dictation"));
        QCOMPARE(transcript->text(), QStringLiteral("Nothing dictated yet."));
        QVERIFY(!copy->isVisible());
        QCOMPARE(quit->text(), QStringLiteral("Quit Speecher"));

        emit controller.stateChanged(QStringLiteral("listening"));
        emit controller.statusChanged(QStringLiteral("Listening…"));
        QCOMPARE(heading->text(), QStringLiteral("Listening…"));
        QVERIFY(level->isVisible());
        QCOMPARE(toggle->text(), QStringLiteral("Stop dictation"));

        emit controller.session()->previewChanged(QStringLiteral("hello tray"));
        QCOMPARE(transcript->text(), QStringLiteral("hello tray"));
        QVERIFY(copy->isVisible());
        copy->click();
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("hello tray"));

        // The next dictation clears its preview; the panel keeps offering the
        // last words until new ones arrive.
        emit controller.session()->previewChanged(QString());
        QCOMPARE(transcript->text(), QStringLiteral("hello tray"));

        emit controller.stateChanged(QStringLiteral("refining"));
        QCOMPARE(toggle->text(), QStringLiteral("Cancel refinement"));
        QVERIFY(!level->isVisible());

        // A long error wraps, and the panel grows with it rather than
        // clipping what follows.
        emit controller.statusChanged(QStringLiteral("Refining…"));
        const int shortHeight = panel.height();
        emit controller.statusChanged(QStringLiteral(
            "The transcription service rejected the request: the API key is invalid or has "
            "expired. Sign in again on the Accounts page."));
        QVERIFY(panel.height() > shortHeight);
        QCOMPARE(panel.height(), panel.heightForWidth(panel.width()));
    }

    // Screenshot seam for UI evidence, as on Windows: the panel idle,
    // listening, and offering a transcript.
    void panelEvidenceGrabsForDocumentation()
    {
        const QString grabDir = qEnvironmentVariable("SPEECHER_TEST_GRAB_DIR");
        if (grabDir.isEmpty()) {
            QSKIP("SPEECHER_TEST_GRAB_DIR is not set");
        }
        ApplicationController controller(true);
        TrayStatusPanel panel(&controller);
        panel.show();
        const auto grab = [&](const QString &name) {
            QCoreApplication::processEvents();
            QVERIFY(panel.grab().save(grabDir + QStringLiteral("/tray-panel-%1.png").arg(name)));
        };
        grab(QStringLiteral("idle"));
        emit controller.stateChanged(QStringLiteral("listening"));
        emit controller.statusChanged(QStringLiteral("Listening…"));
        emit controller.audioLevelChanged(0.6f);
        grab(QStringLiteral("listening"));
        emit controller.session()->transcriptDelivered(QStringLiteral(
            "Can we move the design review to Thursday afternoon, and could you send the "
            "updated agenda to everyone on the list before the end of the day so people "
            "have time to read it?"));
        emit controller.stateChanged(QStringLiteral("idle"));
        emit controller.statusChanged(QStringLiteral("Idle"));
        grab(QStringLiteral("transcript"));
    }

private:
    static QWidget *visibleAppWindow()
    {
        for (QWidget *candidate : QApplication::topLevelWidgets()) {
            if (candidate->objectName() == QStringLiteral("appWindow") && candidate->isVisible()) {
                return candidate;
            }
        }
        return nullptr;
    }
};

int runLinuxTrayTests(int argc, char **argv)
{
    LinuxTrayTests suite;
    return runTestSuite(&suite, argc, argv);
}

#include "test_linux_tray.moc"
