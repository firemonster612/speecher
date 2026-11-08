#include "common/test_suites.h"

#include "app/AppFrontEnd.h"
#include "app/ApplicationController.h"
#include "app/TrayIcon.h"
#include "core/SettingsStore.h"
#include "core/history/HistoryStore.h"
#include "dictation/DictationSession.h"
#include "recording/RecordingPresentation.h"

#include <QAction>
#include <QMenu>
#include <QScopeGuard>
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
        QCOMPARE(actions.size(), 6);
        QCOMPARE(actions.at(0)->objectName(), QStringLiteral("trayToggleDictation"));
        QCOMPARE(actions.at(0)->text(), QStringLiteral("Start dictation"));
        QCOMPARE(actions.at(1)->objectName(), QStringLiteral("trayPasteAgain"));
        QCOMPARE(actions.at(1)->text(), QStringLiteral("Paste last dictation"));
        QCOMPARE(actions.at(2)->objectName(), QStringLiteral("trayToggleRecording"));
        QCOMPARE(actions.at(2)->text(), QStringLiteral("Start recording"));
        QCOMPARE(actions.at(3)->objectName(), QStringLiteral("traySettings"));
        QCOMPARE(actions.at(3)->text(), QStringLiteral("Settings"));
        QVERIFY(actions.at(4)->isSeparator());
        QCOMPARE(actions.at(5)->objectName(), QStringLiteral("trayQuit"));
        QCOMPARE(actions.at(5)->text(), QStringLiteral("Quit Speecher"));
    }

    // Start recording asks for the consent notice before the first recording
    // ever, and starts one after; while one runs the item and the tooltip
    // say for how long.
    void recordingItemStartsAndStopsARecording()
    {
        ApplicationController controller(true);
        // Setup is still to run, so a start ends there, before any
        // microphone permission.
        WindowRequests windows;
        controller.setFrontEnd(&windows);
        const auto detach = qScopeGuard([&controller] { controller.setFrontEnd(nullptr); });
        TrayIcon tray(&controller);
        QAction *item = tray.findChild<QSystemTrayIcon *>()->contextMenu()->findChild<QAction *>(
            QStringLiteral("trayToggleRecording"));
        QVERIFY(item);
        QSignalSpy consent(&controller, &ApplicationController::recordingConsentNeeded);
        QSignalSpy starts(&controller, &ApplicationController::recordingStartFinished);
        item->trigger();
        QCOMPARE(consent.count(), 1);
        QCOMPARE(starts.count(), 0);
        controller.settings()->setRecordingConsentNoticeShown(true);
        item->trigger();
        // Setup isn't finished here, and recording doesn't wait for it: the
        // start gets past setup to the microphone and the speech service,
        // which this test machine doesn't have, and says why it stopped.
        QTRY_COMPARE(starts.count(), 1);
        QVERIFY(!starts.first().first().toString().isEmpty());
        QCOMPARE(consent.count(), 1);

        QCOMPARE(trayRecordingActionText(false, true, 723000), QStringLiteral("Stop recording (12:03)"));
        QCOMPARE(trayRecordingActionText(true, false, 0), QStringLiteral("Cancel recording"));
        QCOMPARE(recordingElapsedText(3723000), QStringLiteral("1:02:03"));
        QCOMPARE(trayToolTip(false, QStringLiteral("12:03")), QStringLiteral("Speecher, recording a call, 12:03"));
        QCOMPARE(trayToolTip(true, QStringLiteral("12:03")),
                 QStringLiteral("Speecher is listening, recording a call, 12:03"));
        QCOMPARE(trayToolTip(true, QString()), QStringLiteral("Speecher is listening"));
    }

    // Paste last dictation is offered once a dictation finished, and never
    // for one into a password field.
    void pasteLastDictationFollowsTheLastDictation()
    {
        ApplicationController controller(true);
        TrayIcon tray(&controller);
        QMenu *menu = tray.findChild<QSystemTrayIcon *>()->contextMenu();
        QAction *pasteAgain = menu->findChild<QAction *>(QStringLiteral("trayPasteAgain"));
        QVERIFY(pasteAgain);
        emit menu->aboutToShow();
        QVERIFY(!pasteAgain->isEnabled());

        HistoryEntry entry;
        entry.kept = HistoryKeep::Nothing;
        emit controller.session()->dictationFinished(entry, true);
        emit menu->aboutToShow();
        QVERIFY(!pasteAgain->isEnabled());
        QVERIFY(controller.pasteAgainText().isEmpty());
    }

    // After a restart, History's newest dictation, while it keeps text.
    void pasteLastDictationReadsHistory()
    {
        ApplicationController controller(true);
        controller.history()->clear({}, {}, true);
        QVERIFY(controller.settings()->edit([](AppSettings &draft) {
            draft.history.keeps = HistoryKeep::Text;
            return true;
        }));
        HistoryEntry entry;
        entry.stats.finishedAt = QDateTime::currentDateTime();
        entry.kept = HistoryKeep::Text;
        entry.text = QStringLiteral("Move the review to Thursday.");
        controller.history()->add(entry);
        TrayIcon tray(&controller);
        QMenu *menu = tray.findChild<QSystemTrayIcon *>()->contextMenu();
        emit menu->aboutToShow();
        QVERIFY(menu->findChild<QAction *>(QStringLiteral("trayPasteAgain"))->isEnabled());
        QCOMPARE(controller.pasteAgainText(), QStringLiteral("Move the review to Thursday."));
        controller.history()->clear({}, {}, true);
        controller.settings()->raw().clear();
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
        SettingsStore().edit([](AppSettings &draft) { draft.setupCompleted = true; return true; });
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
