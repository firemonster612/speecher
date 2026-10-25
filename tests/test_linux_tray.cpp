#include "common/test_suites.h"

#include "app/AppFrontEnd.h"
#include "app/ApplicationController.h"
#include "app/LinuxTrayIcon.h"
#include "core/SettingsStore.h"

#include <QAction>
#include <QMenu>
#include <QSignalSpy>
#include <QSystemTrayIcon>

using namespace speecher;

namespace {

// Records which windows the controller asks for.
class WindowRequests final : public AppFrontEnd {
public:
    QStringList requests;

    void showMainWindow() override { requests.append(QStringLiteral("main")); }
    void hideMainWindow() override {}
    void showSettingsWindow() override { requests.append(QStringLiteral("settings")); }
    void showSetupAssistant(SetupAssistantPage) override { requests.append(QStringLiteral("setup")); }
    void showTranscribeFiles(const QStringList &) override {}
    void showDictationError(const QString &, const PopupErrorAction &) override {}
    void alert() override {}
    void notifyIfNoWindowShown(const QString &, const QString &, const QString &) override {}
};

} // namespace

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
        QCOMPARE(actions.at(1)->text(), QStringLiteral("Settings"));
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

    void clickOpensSpeecherAndSettingsOpensSettings()
    {
        SettingsStore().setSetupCompleted(true);
        ApplicationController controller(true);
        WindowRequests windows;
        controller.setFrontEnd(&windows);
        LinuxTrayIcon tray(&controller);
        auto *icon = tray.findChild<QSystemTrayIcon *>();

        emit icon->activated(QSystemTrayIcon::Trigger);
        QCOMPARE(windows.requests, QStringList{QStringLiteral("main")});

        icon->contextMenu()->findChild<QAction *>(QStringLiteral("traySettings"))->trigger();
        QCOMPARE(windows.requests, (QStringList{QStringLiteral("main"), QStringLiteral("settings")}));
        controller.setFrontEnd(nullptr);
    }
};

int runLinuxTrayTests(int argc, char **argv)
{
    LinuxTrayTests suite;
    return runTestSuite(&suite, argc, argv);
}

#include "test_linux_tray.moc"
