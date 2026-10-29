#include "common/test_suites.h"

#include "app/AppFrontEnd.h"
#include "app/ApplicationController.h"
#include "app/TrayIcon.h"
#include "core/SettingsStore.h"

#include <QAction>
#include <QMenu>
#include <QSignalSpy>
#include <QSystemTrayIcon>
#ifdef Q_OS_WIN
#include <QImage>
#include <QSettings>
#endif

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

class TrayTests : public QObject {
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
        TrayIcon tray(&controller);

        auto *icon = tray.findChild<QSystemTrayIcon *>();
        QVERIFY(icon);
        QCOMPARE(icon->toolTip(), QStringLiteral("Speecher"));

        QMenu *menu = icon->contextMenu();
        QVERIFY(menu);
        QList<QAction *> actions = menu->actions();
#ifndef Q_OS_LINUX
        // Where a click opens the menu, the menu opens Speecher.
        QVERIFY(!actions.isEmpty());
        QCOMPARE(actions.first()->objectName(), QStringLiteral("trayOpen"));
        QCOMPARE(actions.takeFirst()->text(), QStringLiteral("Open Speecher"));
#endif
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
        TrayIcon tray(&controller);
        auto *icon = tray.findChild<QSystemTrayIcon *>();
        auto *toggle = icon->contextMenu()->findChild<QAction *>(QStringLiteral("trayToggleDictation"));

        emit controller.stateChanged(DictationState::Listening);
        QCOMPARE(toggle->text(), QStringLiteral("Stop dictation"));
        QCOMPARE(icon->toolTip(), QStringLiteral("Speecher is listening"));

        emit controller.stateChanged(DictationState::Starting);
        QCOMPARE(toggle->text(), QStringLiteral("Stop dictation"));

        // Toggling mid-refinement cancels the refinement, so the action says
        // so; during stopping/delivering it does nothing.
        emit controller.stateChanged(DictationState::Refining);
        QCOMPARE(toggle->text(), QStringLiteral("Cancel refinement"));
        QVERIFY(toggle->isEnabled());

        emit controller.stateChanged(DictationState::Stopping);
        QVERIFY(!toggle->isEnabled());

        emit controller.stateChanged(DictationState::Delivering);
        QVERIFY(!toggle->isEnabled());

        emit controller.stateChanged(DictationState::Idle);
        QCOMPARE(toggle->text(), QStringLiteral("Start dictation"));
        QVERIFY(toggle->isEnabled());
        QCOMPARE(icon->toolTip(), QStringLiteral("Speecher"));
    }

#ifdef Q_OS_WIN
    // The listening icon is a glyph in the taskbar's text colour, which can
    // differ from the apps' theme, not a themed icon in the apps' colours.
    void listeningIconTakesTheTaskbarColour()
    {
        ApplicationController controller(true);
        TrayIcon tray(&controller);
        auto *icon = tray.findChild<QSystemTrayIcon *>();
        const QSettings personalize(
            QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
            QSettings::NativeFormat);
        const QRgb taskbarText =
            personalize.value(QStringLiteral("SystemUsesLightTheme"), 1).toInt() != 0 ? qRgb(0, 0, 0) : qRgb(255, 255, 255);

        emit controller.stateChanged(DictationState::Listening);
        const QImage image = icon->icon().pixmap(32).toImage();
        int inked = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QRgb pixel = image.pixel(x, y);
                if (qAlpha(pixel) == 255) {
                    QCOMPARE(qRgb(qRed(pixel), qGreen(pixel), qBlue(pixel)), taskbarText);
                    ++inked;
                }
            }
        }
        QVERIFY(inked > 0);
    }
#endif

    void quitActionRequestsApplicationQuit()
    {
        ApplicationController controller(true);
        TrayIcon tray(&controller);
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
        TrayIcon tray(&controller);
        auto *icon = tray.findChild<QSystemTrayIcon *>();

#ifdef Q_OS_MACOS
        // A click there opens the menu, and Open Speecher in it.
        emit icon->activated(QSystemTrayIcon::Trigger);
        QVERIFY(windows.requests.isEmpty());
        icon->contextMenu()->findChild<QAction *>(QStringLiteral("trayOpen"))->trigger();
#else
        emit icon->activated(QSystemTrayIcon::Trigger);
#endif
        QCOMPARE(windows.requests, QStringList{QStringLiteral("main")});

        icon->contextMenu()->findChild<QAction *>(QStringLiteral("traySettings"))->trigger();
        QCOMPARE(windows.requests, (QStringList{QStringLiteral("main"), QStringLiteral("settings")}));
        controller.setFrontEnd(nullptr);
    }
};

int runTrayTests(int argc, char **argv)
{
    TrayTests suite;
    return runTestSuite(&suite, argc, argv);
}

#include "test_tray.moc"
