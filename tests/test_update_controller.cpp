#include "common/test_prelude.h"
#include "common/test_suites.h"
#include "common/test_doubles.h"

#include "app/ApplicationController.h"
#include "app/ManifestUpdater.h"
#ifdef Q_OS_LINUX
#include "app/AppImageUpdater.h"
#endif
#include "core/SettingsStore.h"
#include "core/settings/SettingsSchema.h"
#include "dictation/DictationSession.h"
#include "frontend/qt/QtFrontEnd.h"
#include "ui/AppWindow.h"
#include "ui/TranscriberPopup.h"

#include <QFile>
#include <QFrame>
#include <QLabel>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QNetworkReply>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTimer>

#include <cstring>
#ifdef Q_OS_LINUX
#include <QLocalServer>
#include <sys/stat.h>
#endif

using namespace speecher;

namespace speecher {

class ManifestUpdaterTestAccess {
public:
    static void setState(ManifestUpdater &updater,
                         UpdateController::State state,
                         const QString &error = {})
    {
        updater.setState(state, error);
    }

    static void setAvailableVersion(ManifestUpdater &updater,
                                    const QString &version,
                                    UpdateChannel channel = UpdateChannel::Stable)
    {
        updater.m_manifest.version = version;
        updater.m_manifest.channel = channel;
    }

    static void restartNow(ManifestUpdater &updater)
    {
        updater.restartNow();
    }

    static bool shouldOfferManifest(const UpdateManifest &manifest,
                                    qint64 currentBuildNumber,
                                    const QString &currentVersion,
                                    UpdateChannel channel,
                                    bool automaticCheck)
    {
        return ManifestUpdater::shouldOfferManifest(
            manifest, currentBuildNumber, currentVersion, channel, automaticCheck);
    }

    static void finishCheck(ManifestUpdater &updater,
                            QNetworkReply *reply,
                            bool automatic)
    {
        updater.m_automaticCheck = automatic;
        updater.m_reply = reply;
        updater.finishCheck(reply);
    }

    static int retryInterval(const ManifestUpdater &updater)
    {
        return updater.m_dailyTimer->interval();
    }

    static void setAutomaticCheckFailures(ManifestUpdater &updater, int failures)
    {
        updater.m_automaticCheckFailures = failures;
    }

    static void setManualInstallRequired(ManifestUpdater &updater, bool required)
    {
        updater.m_manualInstallRequired = required;
    }
};

#ifdef Q_OS_LINUX
class AppImageUpdaterTestAccess {
public:
    static void restartAppImage(AppImageUpdater &updater)
    {
        updater.restartApplication();
    }

    static QLocalServer *restartServer(const AppImageUpdater &updater)
    {
        return updater.m_restartServer;
    }

    static QProcessEnvironment restartEnvironment(const QStringList &arguments,
                                                   QProcessEnvironment environment)
    {
        return AppImageUpdater::restartEnvironment(arguments, std::move(environment));
    }
};
#endif

class QtFrontEndTestAccess {
public:
    static QFrame *updateBanner(QtFrontEnd &frontEnd)
    {
        return frontEnd.m_popup->findChild<QFrame *>(QStringLiteral("updateBanner"));
    }

    static QLabel *updateBannerText(QtFrontEnd &frontEnd)
    {
        return frontEnd.m_popup->findChild<QLabel *>(QStringLiteral("updateBannerText"));
    }

    static TranscriberPopup *popup(QtFrontEnd &frontEnd)
    {
        return frontEnd.m_popup;
    }
};

} // namespace speecher

namespace {

using namespace speecher::test;

class StaticNetworkReply final : public QNetworkReply {
public:
    explicit StaticNetworkReply(const QByteArray &body,
                                NetworkError error = NoError,
                                QObject *parent = nullptr)
        : QNetworkReply(parent)
        , m_body(body)
    {
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        if (error != NoError) {
            setError(error, QStringLiteral("timed out"));
        }
    }

    void abort() override {}

protected:
    qint64 readData(char *data, qint64 maximumSize) override
    {
        if (m_offset == m_body.size()) {
            return -1;
        }
        const qint64 size = qMin(maximumSize, m_body.size() - m_offset);
        std::memcpy(data, m_body.constData() + m_offset, size_t(size));
        m_offset += size;
        return size;
    }

private:
    QByteArray m_body;
    qint64 m_offset = 0;
};

struct UpdateTestContext {
    SettingsStore settings;
    std::unique_ptr<FakeAudioInput> audio = std::make_unique<FakeAudioInput>();
    std::unique_ptr<FakeMediaController> media = std::make_unique<FakeMediaController>();
    std::unique_ptr<FakeDelivery> delivery = std::make_unique<FakeDelivery>();
    ProviderRegistry providers;
    FakeSpeechTranscriber *speech = nullptr;
    std::unique_ptr<DictationSession> session;

    explicit UpdateTestContext(bool withSpeechProvider)
    {
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("none"));
        if (withSpeechProvider) {
            registerFakeSpeechProvider(providers, &speech);
        }
        session = std::make_unique<DictationSession>(
            &settings, audio.get(), media.get(), delivery.get(), &providers);
    }
};

class TestManifestUpdater final : public ManifestUpdater {
public:
    TestManifestUpdater(SettingsStore *settings, DictationSession *session)
        : ManifestUpdater(settings,
                          session,
                          QStringLiteral("linux-x86_64"),
                          QStringLiteral("appimage"),
                          QStringLiteral("AppImage"))
    {
    }

    bool isAppImage() const override { return false; }
    bool supportsAutomaticDownloads() const override { return false; }
    int restartCount = 0;

protected:
    std::unique_ptr<QFile> createDownload(QString *, bool *) override { return {}; }
    bool installDownload(const QString &, QString *) override { return false; }
    void restartApplication() override { ++restartCount; }
};

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

QByteArray validManifestJson()
{
    return R"json({
        "version": "0.1.1-nightly.20260901+gabc1234",
        "buildNumber": 123,
        "linux-x86_64": {
            "appimage": "https:\/\/github.com/firemonster612/speecher/releases/download/nightly/Speecher-x86_64.AppImage",
            "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
        },
        "windows-x86_64": {
            "installer": "https:\/\/github.com/firemonster612/speecher/releases/download/nightly/Speecher-Setup-x64.exe",
            "sha256": "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"
        }
    })json";
}

void writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(contents), contents.size());
}

} // namespace

class UpdateControllerTests : public QObject {
    Q_OBJECT

private slots:
    void releaseNotesNeverExceedTheRunningVersion()
    {
        SchemaContext context;
        context.currentVersion = QStringLiteral("0.1.0");
        const SettingsSchema schema = buildSettingsSchema(context);
        const SettingsPage &page = schema.page(QStringLiteral("whatsNew"));
        const SettingsRow *notes = nullptr;
        for (const SettingsSection &section : page.sections) {
            for (const SettingsRow &row : section.rows) {
                if (row.id == QStringLiteral("whatsNewNotes")) {
                    notes = &row;
                }
            }
        }

        QVERIFY(notes);
        const QString markdown = notes->value(AppSettings{}).toString();
        QVERIFY(markdown.contains(QStringLiteral("# Speecher 0.1.0")));
        QVERIFY(!markdown.contains(QStringLiteral("# Speecher 0.1.1")));
    }

    void parsesManifestAndComparesBuildNumbers()
    {
        QString error;
        const std::optional<UpdateManifest> manifest =
            ManifestUpdater::parseManifest(validManifestJson(),
                                            QStringLiteral("linux-x86_64"),
                                            QStringLiteral("appimage"),
                                            &error);

        QVERIFY2(manifest.has_value(), qPrintable(error));
        QCOMPARE(manifest->version, QStringLiteral("0.1.1-nightly.20260901+gabc1234"));
        QCOMPARE(manifest->buildNumber, 123);
        QCOMPARE(manifest->downloadUrl.toString(),
                 QStringLiteral("https://github.com/firemonster612/speecher/releases/download/nightly/Speecher-x86_64.AppImage"));
        QCOMPARE(manifest->sha256,
                 QByteArrayLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
        QVERIFY(ManifestUpdater::isNewerBuild(*manifest, 122));
        QVERIFY(!ManifestUpdater::isNewerBuild(*manifest, 123));
        QVERIFY(!ManifestUpdater::isNewerBuild(*manifest, 124));

        const std::optional<UpdateManifest> windows =
            ManifestUpdater::parseManifest(validManifestJson(),
                                            QStringLiteral("windows-x86_64"),
                                            QStringLiteral("installer"),
                                            &error);
        QVERIFY2(windows.has_value(), qPrintable(error));
        QCOMPARE(windows->downloadUrl.toString(),
                 QStringLiteral("https://github.com/firemonster612/speecher/releases/download/nightly/Speecher-Setup-x64.exe"));
        QCOMPARE(windows->sha256,
                 QByteArrayLiteral("abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"));
    }

    void manualStableCheckCanOfferAnOlderBuildToANightlyUser()
    {
        UpdateManifest stable;
        stable.version = QStringLiteral("0.1.0");
        stable.buildNumber = 145;

        QVERIFY(ManifestUpdaterTestAccess::shouldOfferManifest(
            stable,
            150,
            QStringLiteral("0.1.1-nightly.20260904+gnightly"),
            UpdateChannel::Stable,
            false));
        QVERIFY(!ManifestUpdaterTestAccess::shouldOfferManifest(
            stable,
            150,
            QStringLiteral("0.1.1-nightly.20260904+gnightly"),
            UpdateChannel::Stable,
            true));
        QVERIFY(!ManifestUpdaterTestAccess::shouldOfferManifest(
            stable, 150, QStringLiteral("0.1.1"), UpdateChannel::Stable, false));
    }

    void changingChannelInvalidatesAnAvailableUpdate()
    {
        UpdateTestContext context(true);
        TestManifestUpdater updater(&context.settings, context.session.get());
        ManifestUpdaterTestAccess::setAvailableVersion(
            updater, QStringLiteral("0.1.1"), UpdateChannel::Stable);
        ManifestUpdaterTestAccess::setState(updater,
                                            UpdateController::State::UpdateAvailable);

        context.settings.setUpdateChannel(UpdateChannel::Nightly);

        QCOMPARE(updater.state(), UpdateController::State::Idle);
        QVERIFY(updater.availableVersion().isEmpty());

        ManifestUpdaterTestAccess::setAvailableVersion(
            updater, QStringLiteral("nightly"), UpdateChannel::Nightly);
        ManifestUpdaterTestAccess::setState(updater,
                                            UpdateController::State::Downloading);
        context.settings.setUpdateChannel(UpdateChannel::Stable);
        QCOMPARE(updater.state(), UpdateController::State::Idle);
        QVERIFY(updater.availableVersion().isEmpty());
    }

    void rejectsInvalidManifest_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::newRow("invalid JSON") << QByteArrayLiteral("{");
        QTest::newRow("missing version")
            << validManifestJson().replace(
                   QByteArrayLiteral("\"version\": \"0.1.1-nightly.20260901+gabc1234\","),
                   QByteArray());
        QTest::newRow("negative build")
            << validManifestJson().replace(QByteArrayLiteral("\"buildNumber\": 123"),
                                           QByteArrayLiteral("\"buildNumber\": -1"));
        QTest::newRow("non-HTTPS download")
            << validManifestJson().replace(QByteArrayLiteral("https:\\/\\/"),
                                           QByteArrayLiteral("http:\\/\\/"));
        QTest::newRow("invalid checksum")
            << validManifestJson().replace(
                   QByteArrayLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"),
                   QByteArrayLiteral("not-a-sha256"));
    }

    void rejectsInvalidManifest()
    {
        QFETCH(QByteArray, json);
        QVERIFY(!ManifestUpdater::parseManifest(json,
                                                QStringLiteral("linux-x86_64"),
                                                QStringLiteral("appimage"))
                     .has_value());
    }

    void rejectsDownloadWithMismatchedChecksum()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("downloaded.AppImage"));
        writeFile(path, QByteArrayLiteral("new AppImage"));

        QString error;
        QVERIFY(!ManifestUpdater::verifyDownload(
            path,
            QByteArrayLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"),
            QStringLiteral("AppImage"),
            &error));
        QVERIFY(error.contains(QStringLiteral("SHA-256")));
    }

#ifdef Q_OS_LINUX
    void swapsAppImageAndPreservesPermissionsWithOwnerExecute()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString installedPath = directory.filePath(QStringLiteral("Speecher.AppImage"));
        const QString downloadedPath = directory.filePath(QStringLiteral("downloaded.AppImage"));

        writeFile(installedPath, QByteArrayLiteral("old AppImage"));
        writeFile(downloadedPath, QByteArrayLiteral("new AppImage"));
        QCOMPARE(::chmod(QFile::encodeName(installedPath).constData(), 0640), 0);
        QCOMPARE(::chmod(QFile::encodeName(downloadedPath).constData(), 0600), 0);

        QString error;
        const std::optional<AppImageFileIdentity> identity =
            AppImageUpdater::fileIdentity(installedPath, &error);
        QVERIFY2(identity.has_value(), qPrintable(error));
        QVERIFY2(AppImageUpdater::swapAppImage(
                     downloadedPath, installedPath, *identity, &error),
                 qPrintable(error));
        QCOMPARE(readFile(installedPath), QByteArrayLiteral("new AppImage"));
        struct stat status {};
        QCOMPARE(::stat(QFile::encodeName(installedPath).constData(), &status), 0);
        QCOMPARE(int(status.st_mode & 0777), 0740);
        QVERIFY(!QFileInfo::exists(downloadedPath));
    }

    void abortsSwapWhenInstalledAppImageChanged()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString installedPath = directory.filePath(QStringLiteral("Speecher.AppImage"));
        const QString downloadedPath = directory.filePath(QStringLiteral("downloaded.AppImage"));
        const QString manualPath = directory.filePath(QStringLiteral("manual.AppImage"));
        writeFile(installedPath, QByteArrayLiteral("old AppImage"));
        writeFile(downloadedPath, QByteArrayLiteral("verified update"));
        writeFile(manualPath, QByteArrayLiteral("manually installed AppImage"));

        QString error;
        const std::optional<AppImageFileIdentity> identity =
            AppImageUpdater::fileIdentity(installedPath, &error);
        QVERIFY2(identity.has_value(), qPrintable(error));
        QVERIFY(QFile::remove(installedPath));
        QVERIFY(QFile::rename(manualPath, installedPath));

        QVERIFY(!AppImageUpdater::swapAppImage(
            downloadedPath, installedPath, *identity, &error));
        QCOMPARE(readFile(installedPath), QByteArrayLiteral("manually installed AppImage"));
        QCOMPARE(readFile(downloadedPath), QByteArrayLiteral("verified update"));
        QVERIFY(error.contains(QStringLiteral("changed since the download started")));
    }

    void reportsSwapFailureInReadOnlyDirectory()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString installedPath = directory.filePath(QStringLiteral("Speecher.AppImage"));
        const QString downloadedPath = directory.filePath(QStringLiteral("downloaded.AppImage"));
        writeFile(installedPath, QByteArrayLiteral("old AppImage"));
        writeFile(downloadedPath, QByteArrayLiteral("verified update"));

        QString error;
        const std::optional<AppImageFileIdentity> identity =
            AppImageUpdater::fileIdentity(installedPath, &error);
        QVERIFY2(identity.has_value(), qPrintable(error));
        const QFileDevice::Permissions permissions = QFile::permissions(directory.path());
        QVERIFY(QFile::setPermissions(directory.path(),
                                      QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        const bool swapped = AppImageUpdater::swapAppImage(
            downloadedPath, installedPath, *identity, &error);
        QVERIFY(QFile::setPermissions(directory.path(), permissions));

        QVERIFY(!swapped);
        QCOMPARE(readFile(installedPath), QByteArrayLiteral("old AppImage"));
        QCOMPARE(readFile(downloadedPath), QByteArrayLiteral("verified update"));
        QVERIFY(error.startsWith(QStringLiteral("Could not install the new AppImage:")));
    }

#ifdef Q_OS_LINUX
    // fsync() on a FIFO fails with EINVAL on Linux; macOS accepts it, and the
    // AppImage swap only runs on Linux anyway.
    void refusesSwapWhenDownloadedFileCannotBeSynchronized()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString installedPath = directory.filePath(QStringLiteral("Speecher.AppImage"));
        const QString downloadedPath = directory.filePath(QStringLiteral("downloaded.AppImage"));
        writeFile(installedPath, QByteArrayLiteral("old AppImage"));
        QCOMPARE(::mkfifo(QFile::encodeName(downloadedPath).constData(), 0600), 0);

        QString error;
        const std::optional<AppImageFileIdentity> identity =
            AppImageUpdater::fileIdentity(installedPath, &error);
        QVERIFY2(identity.has_value(), qPrintable(error));
        QVERIFY(!AppImageUpdater::swapAppImage(
            downloadedPath, installedPath, *identity, &error));
        QCOMPARE(readFile(installedPath), QByteArrayLiteral("old AppImage"));
        QVERIFY(error.contains(QStringLiteral("synchronize")));
    }
#endif

    void unwritableAppImageDirectoryOpensTheReleasePage()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString appImagePath = directory.filePath(QStringLiteral("Speecher.AppImage"));
        writeFile(appImagePath, QByteArrayLiteral("installed AppImage"));

        const QByteArray oldAppImage = qgetenv("APPIMAGE");
        qputenv("APPIMAGE", QFile::encodeName(appImagePath));
        const auto restoreAppImage = qScopeGuard([oldAppImage] {
            if (oldAppImage.isEmpty()) {
                qunsetenv("APPIMAGE");
            } else {
                qputenv("APPIMAGE", oldAppImage);
            }
        });

        const QFileDevice::Permissions permissions = QFile::permissions(directory.path());
        QVERIFY(QFile::setPermissions(directory.path(),
                                      QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        UpdateTestContext context(true);
        AppImageUpdater updater(&context.settings, context.session.get());
        ManifestUpdaterTestAccess::setAvailableVersion(
            updater, QStringLiteral("0.1.1"), context.settings.updateChannel());
        ManifestUpdaterTestAccess::setState(updater,
                                            UpdateController::State::UpdateAvailable);
        QSignalSpy releasePage(&updater, &UpdateController::openReleasePageRequested);

        updater.updateNow();
        QVERIFY(QFile::setPermissions(directory.path(), permissions));

        QCOMPARE(updater.state(), UpdateController::State::Error);
        QCOMPARE(releasePage.count(), 1);
        QVERIFY(updater.errorMessage().contains(QStringLiteral("release page")));
    }
#endif

    void restartPendingAllowsSessionErrorAndRestartsOnIdle()
    {
        UpdateTestContext errorContext(false);
        errorContext.session->startListening();
        QCOMPARE(errorContext.session->state(), DictationState::Error);
        TestManifestUpdater errorUpdater(&errorContext.settings, errorContext.session.get());
        ManifestUpdaterTestAccess::setState(errorUpdater,
                                            UpdateController::State::ReadyToRestart);
        ManifestUpdaterTestAccess::restartNow(errorUpdater);
        QCOMPARE(errorUpdater.state(), UpdateController::State::ReadyToRestart);
        QCOMPARE(errorUpdater.restartCount, 1);

        UpdateTestContext pendingContext(true);
        pendingContext.session->startListening();
        QCOMPARE(pendingContext.session->state(), DictationState::Starting);
        TestManifestUpdater pendingUpdater(&pendingContext.settings,
                                           pendingContext.session.get());
        ManifestUpdaterTestAccess::setState(pendingUpdater,
                                            UpdateController::State::ReadyToRestart);
        ManifestUpdaterTestAccess::restartNow(pendingUpdater);
        QCOMPARE(pendingUpdater.state(), UpdateController::State::RestartPending);
        pendingContext.session->stopListening();
        QCOMPARE(pendingContext.session->state(), DictationState::Idle);
        QCOMPARE(pendingUpdater.restartCount, 1);
    }

    void installAndRestartWritesRestoreStateBeforeRestarting()
    {
        UpdateTestContext context(true);
        TestManifestUpdater updater(&context.settings, context.session.get());
        updater.setRestoreStateProvider([] { return QStringLiteral("settings"); });
        ManifestUpdaterTestAccess::setState(updater,
                                            UpdateController::State::ReadyToRestart);
        updater.installAndRestart();
        QCOMPARE(updater.restartCount, 1);
        QCOMPARE(context.settings.updatesRestoreState(), QStringLiteral("settings"));
        // The write is stamped so a relaunch can tell a fresh token from one a
        // failed restart left behind.
        QVERIFY(context.settings.updatesRestoreStateTime() > 0);
        // Clearing the token drops its timestamp too, so an empty snapshot on a
        // retry can't leave a stale one behind.
        context.settings.setUpdatesRestoreState({});
        QVERIFY(context.settings.updatesRestoreState().isEmpty());
        QCOMPARE(context.settings.updatesRestoreStateTime(), qint64(0));

        // A restart deferred to the end of a dictation restores what the user
        // was doing when they asked, not the idle state the restart waited for.
        UpdateTestContext pending(true);
        TestManifestUpdater pendingUpdater(&pending.settings, pending.session.get());
        pending.session->startListening();
        QCOMPARE(pending.session->state(), DictationState::Starting);
        // Restoring the microphone is deliberately not on offer, so the state
        // that must survive the wait is the settings window the user had open.
        pendingUpdater.setRestoreStateProvider([&pending] {
            const DictationState state = pending.session->state();
            return state == DictationState::Idle || state == DictationState::Error
                ? QString()
                : QStringLiteral("settings");
        });
        ManifestUpdaterTestAccess::setState(pendingUpdater,
                                            UpdateController::State::ReadyToRestart);
        pendingUpdater.installAndRestart();
        QCOMPARE(pendingUpdater.state(), UpdateController::State::RestartPending);
        pending.session->stopListening();
        QCOMPARE(pendingUpdater.restartCount, 1);
        QCOMPARE(pending.settings.updatesRestoreState(), QStringLiteral("settings"));
    }

    void installAndRestartInErrorStateOnlyRetriesTheCheck()
    {
        // Point the check at a closed port so the retry cannot reach GitHub.
        qputenv("SPEECHER_UPDATE_MANIFEST_URL",
                QByteArrayLiteral("https://127.0.0.1:1/update-manifest.json"));
        const auto restoreUrl = qScopeGuard(
            [] { qunsetenv("SPEECHER_UPDATE_MANIFEST_URL"); });

        UpdateTestContext context(true);
        TestManifestUpdater updater(&context.settings, context.session.get());
        ManifestUpdaterTestAccess::setState(updater,
                                            UpdateController::State::Error,
                                            QStringLiteral("install failed"));
        updater.installAndRestart();
        QCOMPARE(updater.state(), UpdateController::State::Checking);
        QCOMPARE(updater.restartCount, 0);
    }

#ifndef Q_OS_MACOS
    void whatsNewChipOffersAutoHidesAndDismisses()
    {
        {
            SettingsStore settings;
            settings.raw().clear();
            settings.setUpdatesLastRunVersion(QStringLiteral("0.0.1"));
            settings.raw().setValue(QStringLiteral("updates/lastRunBuildNumber"), 1);
        }
        ApplicationController controller(true);
        QVERIFY(!controller.pendingWhatsNewVersion().isEmpty());
        QtFrontEnd frontEnd(&controller);
        TranscriberPopup *popup = QtFrontEndTestAccess::popup(frontEnd);
        auto *row = popup->findChild<QWidget *>(QStringLiteral("whatsNewRow"));
        auto *message = popup->findChild<QLabel *>(QStringLiteral("whatsNewText"));
        auto *action = popup->findChild<QPushButton *>(QStringLiteral("whatsNewAction"));
        auto *timer = popup->findChild<QTimer *>(QStringLiteral("whatsNewAutoHide"));
        QVERIFY(row && message && action && timer);
        QVERIFY(!row->isHidden());
        QVERIFY(message->text().contains(QStringLiteral("installed")));
        QCOMPARE(action->text(), QStringLiteral("See what's new"));

        // Auto-hide tidies the popup but keeps the offer pending; only the
        // dismiss button drops it.
        popup->showPopup(1);
        QVERIFY(timer->isActive());
        QCOMPARE(timer->interval(), 6000);
        timer->stop();
        timer->setInterval(0);
        timer->start();
        QTest::qWait(20);
        QVERIFY(row->isHidden());
        QVERIFY(!controller.pendingWhatsNewVersion().isEmpty());
        popup->hide();

        // The offer returns on the next popup, through the real show path
        // rather than a hand-called refresh.
        QMetaObject::invokeMethod(controller.session(),
                                  "popupShowRequested",
                                  Qt::DirectConnection,
                                  Q_ARG(quint64, 2));
        QVERIFY(!row->isHidden());

        // Clicking the button asks to open the What's New page.
        QSignalSpy whatsNewSpy(popup, &TranscriberPopup::whatsNewRequested);
        action->click();
        QCOMPARE(whatsNewSpy.count(), 1);

        auto *dismiss = popup->findChild<QPushButton *>(QStringLiteral("whatsNewDismiss"));
        QVERIFY(dismiss);
        dismiss->click();
        QVERIFY(row->isHidden());
        QVERIFY(controller.pendingWhatsNewVersion().isEmpty());
    }
#endif

#ifdef Q_OS_LINUX
    void restartPreservesAppImageExtractAndRunMode()
    {
        QProcessEnvironment environment;
        QProcessEnvironment fromArgument = AppImageUpdaterTestAccess::restartEnvironment(
            {QStringLiteral("Speecher.AppImage"),
             QStringLiteral("--appimage-extract-and-run")},
            environment);
        QCOMPARE(fromArgument.value(QStringLiteral("APPIMAGE_EXTRACT_AND_RUN")),
                 QStringLiteral("1"));

        environment.insert(QStringLiteral("APPIMAGE_EXTRACT_AND_RUN"),
                           QStringLiteral("1"));
        QProcessEnvironment fromEnvironment =
            AppImageUpdaterTestAccess::restartEnvironment({}, environment);
        QCOMPARE(fromEnvironment.value(QStringLiteral("APPIMAGE_EXTRACT_AND_RUN")),
                 QStringLiteral("1"));

        QProcessEnvironment normal =
            AppImageUpdaterTestAccess::restartEnvironment({}, {});
        QVERIFY(!normal.contains(QStringLiteral("APPIMAGE_EXTRACT_AND_RUN")));
    }
#endif

#ifdef Q_OS_LINUX
    // The AppImage restart handshake only exists on Linux; on macOS the
    // runner's deep $TMPDIR also overflows the local-socket name limit.
    void restartHandshakeCannotBeEnteredTwice()
    {
        const QByteArray previousAppImage = qgetenv("APPIMAGE");
        qputenv("APPIMAGE", QByteArrayLiteral("/bin/true"));
        const auto restoreAppImage = qScopeGuard([previousAppImage] {
            previousAppImage.isNull() ? qunsetenv("APPIMAGE")
                                      : qputenv("APPIMAGE", previousAppImage);
        });
        UpdateTestContext context(true);
        AppImageUpdater updater(&context.settings, context.session.get());

        AppImageUpdaterTestAccess::restartAppImage(updater);
        QLocalServer *firstServer = AppImageUpdaterTestAccess::restartServer(updater);
        QVERIFY(firstServer);
        QCOMPARE(updater.state(), UpdateController::State::Restarting);
        AppImageUpdaterTestAccess::restartAppImage(updater);

        QCOMPARE(AppImageUpdaterTestAccess::restartServer(updater), firstServer);
    }
#endif

    void automaticCheckFailuresRetrySoonAndOnlySuccessUpdatesTheTimestamp()
    {
        constexpr qint64 previousSuccess = 42;
        constexpr int minuteMs = 60 * 1000;
        UpdateTestContext context(true);
        context.settings.setUpdatesLastCheckTime(previousSuccess);
        TestManifestUpdater updater(&context.settings, context.session.get());

        // Backoff never retries slower than the configured check interval
        // (30 minutes by default).
        const QList<int> retryMinutes{5, 10, 20, 30, 30, 30};
        for (const int retryMinute : retryMinutes) {
            ManifestUpdaterTestAccess::finishCheck(
                updater,
                new StaticNetworkReply({}, QNetworkReply::TimeoutError, &updater),
                true);
            QCOMPARE(context.settings.updatesLastCheckTime(), previousSuccess);
            QCOMPARE(ManifestUpdaterTestAccess::retryInterval(updater),
                     retryMinute * minuteMs);
        }
        QVERIFY(updater.repeatedAutomaticCheckFailure());

        UpdateTestContext invalidContext(true);
        invalidContext.settings.setUpdatesLastCheckTime(previousSuccess);
        TestManifestUpdater invalidUpdater(&invalidContext.settings,
                                           invalidContext.session.get());
        ManifestUpdaterTestAccess::finishCheck(
            invalidUpdater, new StaticNetworkReply(QByteArrayLiteral("{"),
                                                   QNetworkReply::NoError,
                                                   &invalidUpdater),
            true);
        QCOMPARE(invalidContext.settings.updatesLastCheckTime(), previousSuccess);
        QCOMPARE(ManifestUpdaterTestAccess::retryInterval(invalidUpdater), 5 * minuteMs);

        ManifestUpdaterTestAccess::finishCheck(
            updater, new StaticNetworkReply(validManifestJson(),
                                            QNetworkReply::NoError,
                                            &updater),
            true);
        QVERIFY(context.settings.updatesLastCheckTime() > previousSuccess);
        QCOMPARE(ManifestUpdaterTestAccess::retryInterval(updater), 30 * minuteMs);
        QVERIFY(!updater.repeatedAutomaticCheckFailure());

        // Changing the configured interval reschedules the timer immediately.
        context.settings.setUpdateCheckIntervalMinutes(60);
        QCOMPARE(ManifestUpdaterTestAccess::retryInterval(updater), 60 * minuteMs);
    }

    void updateBannerActionsDescribeWhatTheyDo()
    {
#ifndef Q_OS_MACOS
        ApplicationController controller(true);
        auto *updater = dynamic_cast<ManifestUpdater *>(controller.updates());
        QVERIFY(updater);
        AppWindow window(&controller);

        ManifestUpdaterTestAccess::setManualInstallRequired(*updater, true);
        ManifestUpdaterTestAccess::setState(
            *updater,
            UpdateController::State::Error,
            QStringLiteral("Download the replacement from the release page."));
        auto *action = window.findChild<QPushButton *>(QStringLiteral("updateAction"));
        QVERIFY(action);
        QCOMPARE(action->text(), QStringLiteral("Open release page"));

        ManifestUpdaterTestAccess::setManualInstallRequired(*updater, false);
        ManifestUpdaterTestAccess::setAvailableVersion(
            *updater, QStringLiteral("0.1.1"), UpdateChannel::Stable);
        ManifestUpdaterTestAccess::setState(*updater,
                                            UpdateController::State::UpdateAvailable);
        auto *message = window.findChild<QLabel *>(QStringLiteral("updateBannerText"));
        QVERIFY(message);
        // A Nightly Build is offered a stable replacement; a stable build (the
        // tag builds CI runs) is offered a plain update.
        QCOMPARE(message->text(),
                 updater->stableReplacementAvailable()
                     ? QStringLiteral("Switch to Stable Release 0.1.1 (replaces this Nightly Build)")
                     : QStringLiteral("Speecher 0.1.1 is available"));
#endif
    }

    void bannerAndChipVisibilityFollowUpdateState()
    {
        UpdateTestContext context(true);
        TestManifestUpdater updater(&context.settings, context.session.get());
        for (const UpdateController::State state : {UpdateController::State::Idle,
                                                    UpdateController::State::Checking,
                                                    UpdateController::State::CheckFailed,
                                                    UpdateController::State::UpToDate}) {
            ManifestUpdaterTestAccess::setState(updater, state, QStringLiteral("check failed"));
            QVERIFY(!updater.bannerVisible());
        }
        ManifestUpdaterTestAccess::setAvailableVersion(updater, QStringLiteral("2.0"));
        for (const UpdateController::State state : {UpdateController::State::UpdateAvailable,
                                                    UpdateController::State::Downloading,
                                                    UpdateController::State::ReadyToRestart,
                                                    UpdateController::State::RestartPending,
                                                    UpdateController::State::Restarting,
                                                    UpdateController::State::Error}) {
            ManifestUpdaterTestAccess::setState(updater, state, QStringLiteral("install failed"));
            QVERIFY(updater.bannerVisible());
        }

        // The chip half needs the Linux composition, whose updater is an
        // AppImageUpdater; macOS composes MacSparkleUpdater and Sparkle owns
        // its own UI there.
#ifndef Q_OS_MACOS
        ApplicationController controller(true);
        QtFrontEnd frontEnd(&controller);
        auto *controllerUpdater = dynamic_cast<ManifestUpdater *>(controller.updates());
        QVERIFY(controllerUpdater);
        QFrame *banner = QtFrontEndTestAccess::updateBanner(frontEnd);
        QLabel *message = QtFrontEndTestAccess::updateBannerText(frontEnd);
        QVERIFY(banner && message);
        ManifestUpdaterTestAccess::setAvailableVersion(*controllerUpdater,
                                                       QStringLiteral("2.0"));
        const QList<std::pair<UpdateController::State, bool>> chipStates{
            {UpdateController::State::Idle, false},
            {UpdateController::State::Checking, false},
            {UpdateController::State::CheckFailed, false},
            {UpdateController::State::UpToDate, false},
            {UpdateController::State::UpdateAvailable, true},
            {UpdateController::State::Downloading, true},
            {UpdateController::State::ReadyToRestart, true},
            {UpdateController::State::RestartPending, true},
            {UpdateController::State::Restarting, true},
            {UpdateController::State::Error, true},
        };
        for (const auto &[state, visible] : chipStates) {
            ManifestUpdaterTestAccess::setState(*controllerUpdater,
                                                state,
                                                QStringLiteral("install failed"));
            QCOMPARE(!banner->isHidden(), visible);
        }
        ManifestUpdaterTestAccess::setAutomaticCheckFailures(*controllerUpdater, 3);
        ManifestUpdaterTestAccess::setState(*controllerUpdater,
                                            UpdateController::State::CheckFailed,
                                            QStringLiteral("Could not check for updates"));
        QVERIFY(!banner->isHidden());
        QCOMPARE(message->text(), QStringLiteral("Update check failed"));
        ManifestUpdaterTestAccess::setState(*controllerUpdater,
                                            UpdateController::State::Error,
                                            QStringLiteral("install failed"));
        QVERIFY(!banner->isHidden());
        QCOMPARE(message->text(), QStringLiteral("install failed"));
        ManifestUpdaterTestAccess::setState(*controllerUpdater,
                                            UpdateController::State::Restarting);
        QCOMPARE(message->text(), QStringLiteral("Restarting…"));
#endif
    }

    void downgradeDoesNotCreatePendingWhatsNewState()
    {
        {
            SettingsStore existingSettings;
            existingSettings.raw().clear();
            existingSettings.setUpdatesLastRunVersion(QStringLiteral("9.0.0"));
        }

        ApplicationController controller(true);
        QVERIFY(controller.pendingWhatsNewVersion().isEmpty());
        QCOMPARE(controller.settings()->updatesLastRunVersion(),
                 QStringLiteral(SPEECHER_VERSION));
    }

    void nightlyUpgradeUsesBuildNumberAndKeepsTheFullPreviousVersion()
    {
        const QString previous = QStringLiteral(
            "0.1.1-nightly.20260903+gprevious");
        {
            SettingsStore settings;
            settings.raw().clear();
            settings.setUpdatesLastRunVersion(previous);
            settings.raw().setValue(QStringLiteral("updates/lastRunBuildNumber"),
                                    qint64(SPEECHER_BUILD_NUMBER) - 1);
        }

        ApplicationController controller(true);
        QCOMPARE(controller.pendingWhatsNewVersion(), previous);
        QCOMPARE(controller.settings()->updatesLastRunVersion(),
                 QStringLiteral(SPEECHER_VERSION));
        QCOMPARE(controller.settings()->raw()
                     .value(QStringLiteral("updates/lastRunBuildNumber"))
                     .toLongLong(),
                 qint64(SPEECHER_BUILD_NUMBER));
    }
};

int runUpdateControllerTests(int argc, char **argv)
{
    UpdateControllerTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_update_controller.moc"
