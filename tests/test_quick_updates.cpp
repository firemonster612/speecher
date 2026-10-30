#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "app/TrayIcon.h"
#include "app/WhatsNewTracker.h"
#include "common/test_manifest_updater_access.h"
#include "common/test_quick_windows.h"
#include "core/SettingsStore.h"
#include "dictation/DictationSession.h"
#include "dictation/PopupPresentation.h"
#include "frontend/quick/host/QuickFrontEnd.h"
#include "frontend/quick/host/QuickUpdates.h"
#include "transcribe/FileTranscriptionSession.h"

#include <QDesktopServices>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QUrl>

using namespace speecher;
using speecher::test::openWindow;

namespace {

using State = UpdateController::State;

// An updater that never reaches the network: the tests put it in each state.
class StagedUpdater final : public ManifestUpdater {
public:
    StagedUpdater(SettingsStore *settings, bool automaticDownloads)
        : ManifestUpdater(settings, QStringLiteral("linux-x86_64"), QStringLiteral("appimage"), QStringLiteral("AppImage"))
        , m_automaticDownloads(automaticDownloads)
    {
    }

    bool isAppImage() const override { return m_automaticDownloads; }
    bool supportsAutomaticDownloads() const override { return m_automaticDownloads; }

protected:
    std::unique_ptr<QFile> createDownload(QString *, bool *) override { return {}; }
    bool installDownload(const QString &, QString *) override { return false; }
    void restartApplication() override {}

private:
    bool m_automaticDownloads;
};

// Takes the URLs the app would open in a browser.
class UrlCatcher : public QObject {
    Q_OBJECT

public:
    UrlCatcher() { QDesktopServices::setUrlHandler(QStringLiteral("https"), this, "open"); }
    ~UrlCatcher() override { QDesktopServices::unsetUrlHandler(QStringLiteral("https")); }

    QList<QUrl> opened;

public slots:
    void open(const QUrl &url) { opened.append(url); }
};

// The page the main window shows.
QString mainPage()
{
    QQuickWindow *main = openWindow("Main");
    QuickNavigation *navigation = main ? qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"),
                                                                                       QStringLiteral("Navigation"))
                               : nullptr;
    return navigation ? navigation->current() : QString();
}

} // namespace

// The UI's Updates over a staged updater and the controller's What's New.
class QuickUpdatesTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Frames without a GPU, so a shown window really renders one.
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    }

    void init()
    {
        SettingsStore settings;
        settings.raw().clear();
    }

    void statesFollowTheUpdater_data()
    {
        QTest::addColumn<int>("state");
        QTest::addColumn<QString>("name");
        QTest::newRow("idle") << int(State::Idle) << "idle";
        QTest::newRow("checking") << int(State::Checking) << "checking";
        QTest::newRow("check failed") << int(State::CheckFailed) << "checkFailed";
        QTest::newRow("up to date") << int(State::UpToDate) << "upToDate";
        QTest::newRow("available") << int(State::UpdateAvailable) << "available";
        QTest::newRow("downloading") << int(State::Downloading) << "downloading";
        QTest::newRow("ready") << int(State::ReadyToRestart) << "ready";
        QTest::newRow("restart pending") << int(State::RestartPending) << "restartPending";
        QTest::newRow("restarting") << int(State::Restarting) << "restarting";
        QTest::newRow("error") << int(State::Error) << "error";
    }

    void statesFollowTheUpdater()
    {
        QFETCH(int, state);
        QFETCH(QString, name);
        ApplicationController controller(true);
        StagedUpdater updater(controller.settings(), true);
        QuickUpdates updates(&controller, &updater);
        QSignalSpy changed(&updates, &QuickUpdates::changed);

        ManifestUpdaterTestAccess::setState(updater, State(state), QStringLiteral("Something broke."));
        QCOMPARE(updates.state(), name);
        QCOMPARE(updates.error(), QStringLiteral("Something broke."));
        QCOMPARE(changed.count(), 1);
    }

    void aNightlyOfferIsNamedByItsBuild()
    {
        ApplicationController controller(true);
        StagedUpdater updater(controller.settings(), true);
        QuickUpdates updates(&controller, &updater);

        ManifestUpdaterTestAccess::setAvailableVersion(updater, QStringLiteral("0.2.1-nightly.20260921+gabc1234"),
                                                       UpdateChannel::Nightly, 123);
        ManifestUpdaterTestAccess::setState(updater, State::UpdateAvailable);
        QCOMPARE(updates.availableVersion(), QStringLiteral("nightly build 123 (gabc1234)"));
    }

    // The popup and the sidebar offer an update only while it needs the person.
    void attentionFollowsTheOfferUntilDismissedOrPutOff()
    {
        ApplicationController controller(true);
        StagedUpdater updater(controller.settings(), true);
        QuickUpdates updates(&controller, &updater);

        ManifestUpdaterTestAccess::setAvailableVersion(updater, QStringLiteral("9.0.0"));
        ManifestUpdaterTestAccess::setState(updater, State::UpdateAvailable);
        QVERIFY(updates.attention());
        updates.dismiss();
        QVERIFY(!updates.attention());

        ManifestUpdaterTestAccess::setState(updater, State::ReadyToRestart);
        QVERIFY(updates.attention());
        updates.later();
        QVERIFY(!updates.attention());
        ManifestUpdaterTestAccess::setState(updater, State::RestartPending);
        QVERIFY(!updates.attention());

        // Put off for one version only.
        ManifestUpdaterTestAccess::setAvailableVersion(updater, QStringLiteral("9.0.1"));
        ManifestUpdaterTestAccess::setState(updater, State::ReadyToRestart);
        QVERIFY(updates.attention());

        ManifestUpdaterTestAccess::setState(updater, State::Checking);
        QVERIFY(!updates.attention());
        ManifestUpdaterTestAccess::setState(updater, State::UpToDate);
        QVERIFY(!updates.attention());
    }

    // A check asked for answers on the Updates page; automatic checks speak up
    // once they keep failing.
    void onlyRepeatedAutomaticCheckFailuresAskForAttention()
    {
        ApplicationController controller(true);
        StagedUpdater updater(controller.settings(), true);
        QuickUpdates updates(&controller, &updater);

        ManifestUpdaterTestAccess::setState(updater, State::CheckFailed, QStringLiteral("Host not found."));
        QVERIFY(!updates.attention());
        ManifestUpdaterTestAccess::setAutomaticCheckFailures(updater, 3);
        QVERIFY(updates.attention());

        // A failed download always does, and Dismiss acknowledges it.
        ManifestUpdaterTestAccess::setState(updater, State::Error, QStringLiteral("Could not download."));
        QVERIFY(updates.attention());
        updates.dismiss();
        QCOMPARE(updates.state(), QStringLiteral("idle"));
    }

    // Where Speecher cannot install for itself, the release page stands in.
    void anUpdateItCannotInstallOpensTheReleasePage()
    {
        UrlCatcher browser;
        ApplicationController controller(true);
        StagedUpdater updater(controller.settings(), false);
        QuickUpdates updates(&controller, &updater);
        QVERIFY(!updates.automaticDownloads());

        ManifestUpdaterTestAccess::setAvailableVersion(updater, QStringLiteral("9.0.0"));
        ManifestUpdaterTestAccess::setState(updater, State::UpdateAvailable);
        updates.updateNow();
        QCOMPARE(browser.opened, QList<QUrl>{QUrl(QStringLiteral("https://github.com/firemonster612/speecher/releases"))});

        ManifestUpdaterTestAccess::setManualInstallRequired(updater, true);
        ManifestUpdaterTestAccess::setState(updater, State::Error, QStringLiteral("Read-only folder."));
        QVERIFY(updates.manualInstall());
        updates.updateNow();
        QCOMPARE(browser.opened.size(), 2);
    }

    void restartBlockerFollowsTheSession()
    {
        ApplicationController controller(true);
        StagedUpdater updater(controller.settings(), true);
        auto blocker = UpdateController::RestartBlocker::None;
        updater.setRestartBlockerProvider([&blocker] { return blocker; });
        QuickUpdates updates(&controller, &updater);
        QSignalSpy changed(&updates, &QuickUpdates::changed);
        QCOMPARE(updates.restartBlocker(), QString());

        blocker = UpdateController::RestartBlocker::Dictation;
        emit controller.session()->stateChanged(DictationState::Listening);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(updates.restartBlocker(), QStringLiteral("dictation"));

        blocker = UpdateController::RestartBlocker::Recording;
        emit controller.recordingChanged(true);
        QCOMPARE(changed.count(), 2);
        QCOMPARE(updates.restartBlocker(), QStringLiteral("recording"));

        blocker = UpdateController::RestartBlocker::FileTranscription;
        emit controller.fileTranscription()->fileStarted(0, QString());
        QCOMPARE(changed.count(), 3);
        QCOMPARE(updates.restartBlocker(), QStringLiteral("transcription"));

        blocker = UpdateController::RestartBlocker::UnsavedTranscripts;
        emit controller.fileTranscription()->batchFinished({}, false);
        QCOMPARE(changed.count(), 4);
        QCOMPARE(updates.restartBlocker(), QStringLiteral("unsavedTranscripts"));
    }

    void lastCheckedIsWhenACheckLastSucceeded()
    {
        ApplicationController controller(true);
        StagedUpdater updater(controller.settings(), true);
        QuickUpdates updates(&controller, &updater);
        QVERIFY(!updates.lastChecked().isValid());

        controller.settings()->setUpdatesLastCheckTime(1'790'000'000'000);
        QCOMPARE(updates.lastChecked(), QDateTime::fromMSecsSinceEpoch(1'790'000'000'000));
    }

    // Seeing the news ends the offer, and the news stays for the run.
    void whatsNewIsOfferedUntilSeen()
    {
        const QString lastSeen = QStringLiteral("0.0.9");
        SettingsStore().setUpdatesPendingWhatsNewVersion(lastSeen);
        ApplicationController controller(true);
        StagedUpdater updater(controller.settings(), true);
        QuickUpdates updates(&controller, &updater);
        QSignalSpy pending(&updates, &QuickUpdates::whatsNewPendingChanged);
        QSignalSpy page(&updates, &QuickUpdates::pageRequested);

        QVERIFY(updates.whatsNewPending());
        const QString current = updater.currentVersion().section(u'-', 0, 0);
        QVERIFY(updates.newSince(current));
        QVERIFY(!updates.newSince(lastSeen));
        QVERIFY(!updates.newSince(QStringLiteral("999.0.0")));
        const QVariantList releases = updates.whatsNew().value(QStringLiteral("releases")).toList();
        QVERIFY(!releases.isEmpty());
        const QVariantMap newest = releases.first().toMap();
        QVERIFY(!newest.value(QStringLiteral("sections")).toList().isEmpty());

        updates.showWhatsNew();
        QCOMPARE(page.count(), 1);
        QCOMPARE(page.first().first().toString(), QStringLiteral("updates"));

        updates.acknowledgeWhatsNew();
        QCOMPARE(pending.count(), 1);
        QVERIFY(!updates.whatsNewPending());
        QVERIFY(controller.settings()->updatesPendingWhatsNewVersion().isEmpty());
        QVERIFY(updates.newSince(current));
        QCOMPARE(updates.whatsNew().value(QStringLiteral("releases")).toList(), releases);
    }

    // Main opened for another page builds only that one, so the news stays
    // unseen; opened for nothing in particular, it opens on the news.
    void mainOpensOnTheNewsOnlyWhenNotAskedForAnotherPage()
    {
        SettingsStore().setUpdatesPendingWhatsNewVersion(QStringLiteral("0.0.9"));
        {
            ApplicationController controller(true);
            TrayIcon tray(&controller);
            QuickFrontEnd ui(&controller, &tray);
            ui.showTranscribeFiles({});
            QCOMPARE(mainPage(), QStringLiteral("transcribe"));
            QVERIFY(!controller.whatsNew()->pendingVersion().isEmpty());
        }
        ApplicationController controller(true);
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QCOMPARE(mainPage(), QStringLiteral("updates"));
        QVERIFY(controller.whatsNew()->pendingVersion().isEmpty());
    }

    // What's New takes a turn in each new popup, also when a dictation starts
    // while the last one's receipt is still up.
    void whatsNewComesBackWithEachDictation()
    {
        SettingsStore().setUpdatesPendingWhatsNewVersion(QStringLiteral("0.0.9"));
        ApplicationController controller(true);
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        DictationSession *session = controller.session();
        emit session->stateChanged(DictationState::Starting);
        emit session->popupShowRequested(1);
        QQuickWindow *popup = openWindow("PopupWindow");
        QVERIFY(popup);
        QObject *chip = popup->findChild<QObject *>(QStringLiteral("whatsNewChip"));
        QVERIFY(chip);
        QVERIFY(chip->property("open").toBool());
        // Its turn ends a few seconds in.
        QTRY_VERIFY_WITH_TIMEOUT(!chip->property("open").toBool(), 8000);

        emit session->popupMessageRequested(QStringLiteral("Sent to the app"), PopupOutcome::Inserted);
        emit session->stateChanged(DictationState::Idle);
        emit session->stateChanged(DictationState::Starting);
        emit session->popupShowRequested(2);
        QVERIFY(chip->property("open").toBool());
    }

    void withoutAnUpdateNothingIsNew()
    {
        ApplicationController controller(true);
        StagedUpdater updater(controller.settings(), true);
        QuickUpdates updates(&controller, &updater);

        QVERIFY(!updates.whatsNewPending());
        QVERIFY(!updates.newSince(updater.currentVersion().section(u'-', 0, 0)));
    }
};

int runQuickUpdatesTests(int argc, char **argv)
{
    QuickUpdatesTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_quick_updates.moc"
