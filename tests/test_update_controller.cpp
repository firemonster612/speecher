#include "common/test_prelude.h"
#include "common/test_suites.h"
#include "common/test_doubles.h"
#include "common/test_manifest_updater_access.h"

#include "app/ApplicationController.h"
#include "app/ManifestUpdater.h"
#include "app/WhatsNewTracker.h"
#ifdef Q_OS_LINUX
#include "app/AppImageUpdater.h"
#endif
#include "core/ReleaseNotes.h"
#include "core/SettingsStore.h"
#include "dictation/DictationSession.h"

#include <QFile>
#include <QFrame>
#include <QLabel>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QNetworkReply>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

#include <cstring>
#ifdef Q_OS_LINUX
#include <QLocalServer>
#include <sys/stat.h>
#endif

using namespace speecher;

namespace speecher {

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

    UpdateTestContext() { settings.raw().clear(); }
};

class TestManifestUpdater final : public ManifestUpdater {
public:
    explicit TestManifestUpdater(SettingsStore *settings)
        : ManifestUpdater(settings,
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

QByteArray manifestJson(const QString &version, qint64 buildNumber)
{
    QByteArray json = validManifestJson();
    json.replace("0.1.1-nightly.20260901+gabc1234", version.toUtf8());
    json.replace("\"buildNumber\": 123", "\"buildNumber\": " + QByteArray::number(buildNumber));
    return json;
}

// Serves one JSON body to every request, for driving a real check. The
// request is drained before the response and the socket closes after it, so
// the kernel never answers with a reset that would flake the check.
QTcpServer *manifestServer(QObject *parent, const QByteArray &body, int *requestCount = nullptr)
{
    auto *server = new QTcpServer(parent);
    const bool listening = server->listen(QHostAddress::LocalHost);
    Q_ASSERT(listening);
    QObject::connect(server, &QTcpServer::newConnection, server,
                     [server, body, requestCount] {
        QTcpSocket *socket = server->nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        auto answered = std::make_shared<bool>(false);
        QObject::connect(socket, &QTcpSocket::readyRead, socket,
                         [socket, body, requestCount, answered] {
            socket->readAll();
            if (*answered) {
                return;
            }
            *answered = true;
            if (requestCount) {
                ++*requestCount;
            }
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                          + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n"
                          + body);
            socket->flush();
            socket->disconnectFromHost();
        });
    });
    return server;
}

QByteArray serverUrl(const QTcpServer *server)
{
    return QStringLiteral("http://127.0.0.1:%1/update-manifest.json")
        .arg(server->serverPort())
        .toUtf8();
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

    void manualNightlyCheckCrossesToAStableAtLeastAsNew()
    {
        const QString nightlyVersion = QStringLiteral("0.2.1-nightly.20260921+gabc1234");
        UpdateManifest stable;
        stable.version = QStringLiteral("0.2.0");
        stable.buildNumber = 150;
        stable.channel = UpdateChannel::Stable;

        // A manual check on the Nightly channel crosses to a stable cut from
        // the same commit, but never to an older one, and never automatically.
        QVERIFY(ManifestUpdaterTestAccess::shouldOfferManifest(
            stable, 150, nightlyVersion, UpdateChannel::Nightly, false));
        QVERIFY(!ManifestUpdaterTestAccess::shouldOfferManifest(
            stable, 151, nightlyVersion, UpdateChannel::Nightly, false));
        QVERIFY(!ManifestUpdaterTestAccess::shouldOfferManifest(
            stable, 150, nightlyVersion, UpdateChannel::Nightly, true));

        UpdateManifest nightly = stable;
        nightly.channel = UpdateChannel::Nightly;
        QVERIFY(!ManifestUpdaterTestAccess::shouldOfferManifest(
            nightly, 150, nightlyVersion, UpdateChannel::Nightly, false));
    }

    void bestCandidatePrefersTheNewestBuildAndStableOnTies()
    {
        UpdateManifest nightly;
        nightly.version = QStringLiteral("0.2.1-nightly.20260921+gabc1234");
        nightly.buildNumber = 150;
        nightly.channel = UpdateChannel::Nightly;
        UpdateManifest stable;
        stable.version = QStringLiteral("0.2.0");
        stable.buildNumber = 149;
        stable.channel = UpdateChannel::Stable;

        QCOMPARE(ManifestUpdaterTestAccess::bestCandidate(nightly, stable)->version,
                 nightly.version);
        stable.buildNumber = 151;
        QCOMPARE(ManifestUpdaterTestAccess::bestCandidate(nightly, stable)->version,
                 stable.version);
        // A tie goes to the Stable Release, whose artifact the nightly built
        // from the same commit duplicates.
        stable.buildNumber = 150;
        QCOMPARE(ManifestUpdaterTestAccess::bestCandidate(nightly, stable)->version,
                 stable.version);
        QCOMPARE(ManifestUpdaterTestAccess::bestCandidate(std::nullopt, stable)->version,
                 stable.version);
        QCOMPARE(ManifestUpdaterTestAccess::bestCandidate(nightly, std::nullopt)->version,
                 nightly.version);
        QVERIFY(!ManifestUpdaterTestAccess::bestCandidate(std::nullopt, std::nullopt));
    }

    void nightlyChannelChecksOfferTheNewestStableRelease()
    {
        UpdateTestContext context;
        context.settings.edit([](AppSettings &draft) { draft.updates.channel = UpdateChannel::Nightly; return true; });
        TestManifestUpdater updater(&context.settings);
        const qint64 current = updater.currentBuildNumber();

        // The stable release is the newest build, so the Nightly channel's
        // check offers it, carrying the stable channel for the banner wording.
        QTcpServer *nightlyServer = manifestServer(
            &updater,
            manifestJson(QStringLiteral("0.2.2-nightly.20260921+gabc1234"), current + 1));
        QTcpServer *stableServer =
            manifestServer(&updater, manifestJson(QStringLiteral("0.2.1"), current + 2));
        qputenv("SPEECHER_UPDATE_MANIFEST_URL", serverUrl(nightlyServer));
        qputenv("SPEECHER_UPDATE_STABLE_MANIFEST_URL", serverUrl(stableServer));
        const auto restoreEnv = qScopeGuard([] {
            qunsetenv("SPEECHER_UPDATE_MANIFEST_URL");
            qunsetenv("SPEECHER_UPDATE_STABLE_MANIFEST_URL");
        });

        updater.checkForUpdates(UpdateChannel::Nightly);
        QTRY_COMPARE_WITH_TIMEOUT(updater.state(),
                                  UpdateController::State::UpdateAvailable, 10000);
        QCOMPARE(updater.availableVersion(), QStringLiteral("0.2.1"));
        // The winner is an ordinary offer on the Nightly channel: it names the
        // stable version but never demands the Stable channel's consent step.
        QCOMPARE(updater.availableVersionDisplay(), QStringLiteral("0.2.1"));
        QVERIFY(!updater.stableReplacementAvailable());

        // With the nightly ahead, the check offers the nightly as before.
        context.settings.edit([](AppSettings &draft) { draft.updates.channel = UpdateChannel::Stable; return true; });
        context.settings.edit([](AppSettings &draft) { draft.updates.channel = UpdateChannel::Nightly; return true; });
        QTcpServer *newerNightly = manifestServer(
            &updater,
            manifestJson(QStringLiteral("0.2.2-nightly.20260922+gdef5678"), current + 3));
        qputenv("SPEECHER_UPDATE_MANIFEST_URL", serverUrl(newerNightly));
        updater.checkForUpdates(UpdateChannel::Nightly);
        QTRY_COMPARE_WITH_TIMEOUT(updater.state(),
                                  UpdateController::State::UpdateAvailable, 10000);
        QCOMPARE(updater.availableVersion(),
                 QStringLiteral("0.2.2-nightly.20260922+gdef5678"));
        QCOMPARE(updater.availableVersionDisplay(),
                 QStringLiteral("nightly build %1 (gdef5678)").arg(current + 3));
    }

    void nightlyChecksSurviveTheirOwnFeedFailing()
    {
        UpdateTestContext context;
        context.settings.edit([](AppSettings &draft) { draft.updates.channel = UpdateChannel::Nightly; return true; });
        TestManifestUpdater updater(&context.settings);
        const qint64 current = updater.currentBuildNumber();

        // The nightly feed is down, but the stable one holds the newest build:
        // exactly the release a broken feed must not hide.
        QTcpServer *deadNightly = manifestServer(&updater, QByteArrayLiteral("not json"));
        QTcpServer *stableServer =
            manifestServer(&updater, manifestJson(QStringLiteral("0.2.1"), current + 2));
        qputenv("SPEECHER_UPDATE_MANIFEST_URL", serverUrl(deadNightly));
        qputenv("SPEECHER_UPDATE_STABLE_MANIFEST_URL", serverUrl(stableServer));
        const auto restoreEnv = qScopeGuard([] {
            qunsetenv("SPEECHER_UPDATE_MANIFEST_URL");
            qunsetenv("SPEECHER_UPDATE_STABLE_MANIFEST_URL");
        });
        updater.checkForUpdates(UpdateChannel::Nightly);
        QTRY_COMPARE_WITH_TIMEOUT(updater.state(),
                                  UpdateController::State::UpdateAvailable, 10000);
        QCOMPARE(updater.availableVersion(), QStringLiteral("0.2.1"));

        // With nothing to offer from either leg, the failure is reported
        // rather than papered over as "up to date".
        context.settings.edit([](AppSettings &draft) { draft.updates.channel = UpdateChannel::Stable; return true; });
        context.settings.edit([](AppSettings &draft) { draft.updates.channel = UpdateChannel::Nightly; return true; });
        QTcpServer *oldStable =
            manifestServer(&updater, manifestJson(QStringLiteral("0.1.0"), 1));
        qputenv("SPEECHER_UPDATE_STABLE_MANIFEST_URL", serverUrl(oldStable));
        updater.checkForUpdates(UpdateChannel::Nightly);
        QTRY_COMPARE_WITH_TIMEOUT(updater.state(),
                                  UpdateController::State::CheckFailed, 10000);
    }

    void singleManifestOverrideFetchesExactlyOnce()
    {
        UpdateTestContext context;
        context.settings.edit([](AppSettings &draft) { draft.updates.channel = UpdateChannel::Nightly; return true; });
        TestManifestUpdater updater(&context.settings);

        // The e2e rigs set only SPEECHER_UPDATE_MANIFEST_URL, which points both
        // channels at one file; the secondary fetch must not run against it.
        int requests = 0;
        QTcpServer *server = manifestServer(
            &updater,
            manifestJson(QStringLiteral("0.2.2-nightly.20260921+gabc1234"),
                         updater.currentBuildNumber() + 1),
            &requests);
        qputenv("SPEECHER_UPDATE_MANIFEST_URL", serverUrl(server));
        const auto restoreEnv =
            qScopeGuard([] { qunsetenv("SPEECHER_UPDATE_MANIFEST_URL"); });
        updater.checkForUpdates(UpdateChannel::Nightly);
        QTRY_COMPARE_WITH_TIMEOUT(updater.state(),
                                  UpdateController::State::UpdateAvailable, 10000);
        QCOMPARE(requests, 1);
    }

    void updateBannersNameNightlyBuildsPrecisely()
    {
        UpdateTestContext context;
        TestManifestUpdater updater(&context.settings);
        ManifestUpdaterTestAccess::setAvailableVersion(
            updater, QStringLiteral("0.2.1-nightly.20260921+gabc1234"),
            UpdateChannel::Nightly, 481);
        QCOMPARE(updater.availableVersionDisplay(),
                 QStringLiteral("nightly build 481 (gabc1234)"));

        ManifestUpdaterTestAccess::setAvailableVersion(
            updater, QStringLiteral("0.2.0"), UpdateChannel::Stable, 480);
        QCOMPARE(updater.availableVersionDisplay(), QStringLiteral("0.2.0"));

        // Without a build number the date stands in — the Sparkle appcast path.
        QCOMPARE(nightlyVersionDisplay(QStringLiteral("0.2.1-nightly.20260921+gabc1234")),
                 QStringLiteral("nightly 20260921 (gabc1234)"));
    }

    void changingChannelInvalidatesAnAvailableUpdate()
    {
        UpdateTestContext context;
        TestManifestUpdater updater(&context.settings);
        ManifestUpdaterTestAccess::setAvailableVersion(
            updater, QStringLiteral("0.1.1"), UpdateChannel::Stable);
        ManifestUpdaterTestAccess::setState(updater,
                                            UpdateController::State::UpdateAvailable);

        context.settings.edit([](AppSettings &draft) { draft.updates.channel = UpdateChannel::Nightly; return true; });

        QCOMPARE(updater.state(), UpdateController::State::Idle);
        QVERIFY(updater.availableVersion().isEmpty());

        ManifestUpdaterTestAccess::setAvailableVersion(
            updater, QStringLiteral("nightly"), UpdateChannel::Nightly);
        ManifestUpdaterTestAccess::setState(updater,
                                            UpdateController::State::Downloading);
        context.settings.edit([](AppSettings &draft) { draft.updates.channel = UpdateChannel::Stable; return true; });
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
        UpdateTestContext context;
        AppImageUpdater updater(&context.settings);
        ManifestUpdaterTestAccess::setAvailableVersion(
            updater, QStringLiteral("0.1.1"), context.settings.dictationSnapshot().updates.channel);
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

    void restartWaitsWhileBusyAndResumesOnceIdle()
    {
        UpdateTestContext context;
        TestManifestUpdater updater(&context.settings);
        bool busy = false;
        updater.setRestartBlockerProvider([&busy] {
            return busy ? UpdateController::RestartBlocker::Dictation
                        : UpdateController::RestartBlocker::None;
        });
        ManifestUpdaterTestAccess::setState(updater, UpdateController::State::ReadyToRestart);
        ManifestUpdaterTestAccess::restartNow(updater);
        QCOMPARE(updater.restartCount, 1);

        busy = true;
        ManifestUpdaterTestAccess::restartNow(updater);
        QCOMPARE(updater.state(), UpdateController::State::RestartPending);
        updater.resumePendingRestart();
        QCOMPARE(updater.state(), UpdateController::State::RestartPending);
        QCOMPARE(updater.restartCount, 1);

        busy = false;
        updater.resumePendingRestart();
        QCOMPARE(updater.restartCount, 2);
    }

    void installAndRestartWritesRestoreStateBeforeRestarting()
    {
        UpdateTestContext context;
        TestManifestUpdater updater(&context.settings);
        updater.setReopenMainWindowProvider([] { return true; });
        ManifestUpdaterTestAccess::setState(updater,
                                            UpdateController::State::ReadyToRestart);
        updater.installAndRestart();
        QCOMPARE(updater.restartCount, 1);
        QVERIFY(context.settings.updatesReopenMainWindow());
        // The write is stamped so a relaunch can tell a fresh request from one
        // a failed restart left behind.
        QVERIFY(context.settings.updatesReopenMainWindowTime() > 0);
        // Clearing the request drops its timestamp too, so a retry that leaves
        // the window closed can't leave a stale one behind.
        context.settings.setUpdatesReopenMainWindow(false);
        QVERIFY(!context.settings.updatesReopenMainWindow());
        QCOMPARE(context.settings.updatesReopenMainWindowTime(), qint64(0));

        // A restart deferred to the end of a dictation restores what the user
        // was doing when they asked, not the idle state the restart waited for.
        UpdateTestContext pending;
        TestManifestUpdater pendingUpdater(&pending.settings);
        bool busy = true;
        pendingUpdater.setRestartBlockerProvider([&busy] {
            return busy ? UpdateController::RestartBlocker::Dictation
                        : UpdateController::RestartBlocker::None;
        });
        pendingUpdater.setReopenMainWindowProvider([&busy] { return busy; });
        ManifestUpdaterTestAccess::setState(pendingUpdater,
                                            UpdateController::State::ReadyToRestart);
        pendingUpdater.installAndRestart();
        QCOMPARE(pendingUpdater.state(), UpdateController::State::RestartPending);
        busy = false;
        pendingUpdater.resumePendingRestart();
        QCOMPARE(pendingUpdater.restartCount, 1);
        QVERIFY(pending.settings.updatesReopenMainWindow());
    }

    void installAndRestartInErrorStateOnlyRetriesTheCheck()
    {
        // Point the check at a closed port so the retry cannot reach GitHub.
        qputenv("SPEECHER_UPDATE_MANIFEST_URL",
                QByteArrayLiteral("https://127.0.0.1:1/update-manifest.json"));
        const auto restoreUrl = qScopeGuard(
            [] { qunsetenv("SPEECHER_UPDATE_MANIFEST_URL"); });

        UpdateTestContext context;
        TestManifestUpdater updater(&context.settings);
        ManifestUpdaterTestAccess::setState(updater,
                                            UpdateController::State::Error,
                                            QStringLiteral("install failed"));
        updater.installAndRestart();
        QCOMPARE(updater.state(), UpdateController::State::Checking);
        QCOMPARE(updater.restartCount, 0);
    }

#ifndef Q_OS_MACOS
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
        UpdateTestContext context;
        AppImageUpdater updater(&context.settings);

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
        UpdateTestContext context;
        context.settings.setUpdatesLastCheckTime(previousSuccess);
        TestManifestUpdater updater(&context.settings);

        // Backoff never retries slower than the configured check interval
        // (30 minutes by default).
        const QList<int> retryMinutes{5, 10, 20, 30, 30, 30};
        for (const int retryMinute : retryMinutes) {
            ManifestUpdaterTestAccess::finishCheck(
                updater,
                new StaticNetworkReply({}, QNetworkReply::TimeoutError, &updater),
                true);
            QCOMPARE(context.settings.updatesLastCheckTime(), previousSuccess);
            QCOMPARE(ManifestUpdaterTestAccess::nextCheckDelay(updater),
                     retryMinute * minuteMs);
        }
        QVERIFY(updater.repeatedAutomaticCheckFailure());

        UpdateTestContext invalidContext;
        invalidContext.settings.setUpdatesLastCheckTime(previousSuccess);
        TestManifestUpdater invalidUpdater(&invalidContext.settings);
        ManifestUpdaterTestAccess::finishCheck(
            invalidUpdater, new StaticNetworkReply(QByteArrayLiteral("{"),
                                                   QNetworkReply::NoError,
                                                   &invalidUpdater),
            true);
        QCOMPARE(invalidContext.settings.updatesLastCheckTime(), previousSuccess);
        QCOMPARE(ManifestUpdaterTestAccess::nextCheckDelay(invalidUpdater), 5 * minuteMs);

        ManifestUpdaterTestAccess::finishCheck(
            updater, new StaticNetworkReply(validManifestJson(),
                                            QNetworkReply::NoError,
                                            &updater),
            true);
        QVERIFY(context.settings.updatesLastCheckTime() > previousSuccess);
        // The next check waits the interval from the success just recorded,
        // give or take the milliseconds between the two clock reads.
        QVERIFY(qAbs(ManifestUpdaterTestAccess::nextCheckDelay(updater) - 30 * minuteMs) < 1000);
        QVERIFY(!updater.repeatedAutomaticCheckFailure());

        // Changing the configured interval reschedules the timer immediately.
        context.settings.edit([](AppSettings &draft) { draft.updates.checkIntervalMinutes = 60; return true; });
        QVERIFY(qAbs(ManifestUpdaterTestAccess::nextCheckDelay(updater) - 60 * minuteMs) < 1000);
    }

    void noCheckTimerRunsWhileAutomaticChecksAreOff()
    {
        UpdateTestContext context;
        context.settings.setUpdatesLastCheckTime(0);
        TestManifestUpdater updater(&context.settings);
        context.settings.edit([](AppSettings &draft) { draft.updates.autoCheck = false; return true; });
        QVERIFY(!ManifestUpdaterTestAccess::checkTimerActive(updater));
        context.settings.edit([](AppSettings &draft) { draft.updates.autoCheck = true; return true; });
        QVERIFY(ManifestUpdaterTestAccess::checkTimerActive(updater));

        // The off switch stops a failed check's retry, and the on switch restarts it.
        ManifestUpdaterTestAccess::setAutomaticCheckFailures(updater, 1);
        context.settings.edit([](AppSettings &draft) { draft.updates.autoCheck = false; return true; });
        QVERIFY(!ManifestUpdaterTestAccess::checkTimerActive(updater));
        context.settings.edit([](AppSettings &draft) { draft.updates.autoCheck = true; return true; });
        QVERIFY(ManifestUpdaterTestAccess::checkTimerActive(updater));
    }

    // Long intervals wake hourly and ask again, so a week survives sleep and
    // QTimer's range; a due check that cannot start is asked about each minute.
    void automaticChecksWaitUntilDueAndWakeAtLeastHourly()
    {
        constexpr qint64 minuteMs = 60 * 1000;
        constexpr qint64 last = 1'000'000'000'000;
        QCOMPARE(automaticCheckDelayMs(last, 30, last + 10 * minuteMs), int(20 * minuteMs));
        QCOMPARE(automaticCheckDelayMs(last, 7 * 24 * 60, last + minuteMs), int(60 * minuteMs));
        QCOMPARE(automaticCheckDelayMs(last, 30 * 24 * 60, last), int(60 * minuteMs));
        QCOMPARE(automaticCheckDelayMs(last, 30, last + 30 * minuteMs), int(minuteMs));
        QVERIFY(!automaticCheckDue(last, 7 * 24 * 60, last + 6 * 24 * 60 * minuteMs));
        QVERIFY(automaticCheckDue(last, 7 * 24 * 60, last + 7 * 24 * 60 * minuteMs));
        // A last check in the future: the clock went back.
        QVERIFY(automaticCheckDue(last, 30, last - minuteMs));
    }

    void baseVersionsCompareNumericallyWithoutNightlySuffixes()
    {
        QCOMPARE(compareBaseVersions(QStringLiteral("0.2.0-nightly.20260901+gabc1234"),
                                     QStringLiteral("0.2.0")),
                 0);
        QVERIFY(compareBaseVersions(QStringLiteral("0.10.0"), QStringLiteral("0.2.0")) > 0);
        QVERIFY(compareBaseVersions(QStringLiteral("0.2.0"), QStringLiteral("0.2.1")) < 0);
        QCOMPARE(compareBaseVersions(QStringLiteral("0.2.9+g1"), QStringLiteral("0.2.9")), 0);
        QVERIFY(compareBaseVersions(QString(), QStringLiteral("0.1.0")) < 0);
    }

    void releaseNotesShowOnlyThisPlatformsBullets()
    {
        const QString notes = QStringLiteral("## Added\n\n- A shared change that wraps\n  onto a second line.\n"
                                             "- Linux: the tray.\n\n## Fixed\n\n- Windows: a fix.");
        QCOMPARE(releaseNotesForPlatform(notes, QStringLiteral("macOS")),
                 QStringLiteral("## Added\n\n- A shared change that wraps onto a second line."));
    }

    void releaseNotesDropThisPlatformsPrefixAndKeepNestedBullets()
    {
        const QString notes = QStringLiteral("- Linux: the tray, with\n  two parts:\n  - a menu\n  - a panel\n"
                                             "- Windows: a fix:\n  - its detail");
        QCOMPARE(releaseNotesForPlatform(notes, QStringLiteral("Linux")),
                 QStringLiteral("- The tray, with two parts:\n  - a menu\n  - a panel"));
    }

    // Every docs/releases note is built in, newest first.
    void bundledReleaseNotesComeNewestFirst()
    {
        const QList<ReleaseNote> notes = bundledReleaseNotes();
        QVERIFY(!notes.isEmpty());
        for (qsizetype index = 1; index < notes.size(); ++index) {
            QVERIFY2(compareBaseVersions(notes.at(index - 1).version, notes.at(index).version) > 0,
                     qPrintable(notes.at(index).version));
        }
        QCOMPARE(notes.last().version, QStringLiteral("0.1.0"));
        // docs/releases/README.md: every note starts at a level-two heading.
        QVERIFY(notes.first().markdown.startsWith(QStringLiteral("## ")));
    }

    void historyRecordsSplitIntoCommits()
    {
        const QChar rs(0x1e);
        const QChar us(0x1f);
        const QString log = rs + QStringLiteral("def5678") + us + QStringLiteral("Add the thing (#91)") + us
            + QStringLiteral("A longer description.\n\nOver two paragraphs.\n") + rs + QStringLiteral("abc1234")
            + us + QStringLiteral("Tidy the tray icon") + us + QStringLiteral("\n") + rs
            + QStringLiteral("truncated");
        const QList<HistoryCommit> commits = parseHistory(log);
        QCOMPARE(commits.size(), 2);
        QCOMPARE(commits.at(0).sha, QStringLiteral("def5678"));
        QCOMPARE(commits.at(0).subject, QStringLiteral("Add the thing (#91)"));
        QCOMPARE(commits.at(0).body, QStringLiteral("A longer description.\n\nOver two paragraphs."));
        QCOMPARE(commits.at(1).subject, QStringLiteral("Tidy the tray icon"));
        QVERIFY(commits.at(1).body.isEmpty());
    }

    void releaseNotesSplitAtTheirHeadings()
    {
        const QList<ReleaseNoteSection> sections = releaseNoteSections(
            QStringLiteral("## Added\n\n- The tray, with two parts:\n  - a menu\n\n## Fixed\n\n- A crash."));
        QCOMPARE(sections.size(), 2);
        QCOMPARE(sections.at(0).title, QStringLiteral("Added"));
        QCOMPARE(sections.at(0).items, (QStringList{QStringLiteral("The tray, with two parts:"), QStringLiteral("a menu")}));
        QCOMPARE(sections.at(1).title, QStringLiteral("Fixed"));
        QCOMPARE(sections.at(1).items, QStringList{QStringLiteral("A crash.")});
    }

    // Newest first, as bundledReleaseNotes() gives them.
    static QList<ReleaseNote> sampleNotes()
    {
        return {{QStringLiteral("0.3.0"), QStringLiteral("three")},
                {QStringLiteral("0.2.1"), QStringLiteral("two-one")},
                {QStringLiteral("0.2.0"), QStringLiteral("two")}};
    }

    static QStringList versions(const QList<ReleaseNote> &notes)
    {
        QStringList names;
        for (const ReleaseNote &note : notes) {
            names.append(note.version);
        }
        return names;
    }

    void whatsNewListsTheStableReleasesAnUpdateCrossed()
    {
        const WhatsNew news = whatsNew(sampleNotes(), {}, QStringLiteral("0.2.0"), QStringLiteral("0.3.0"));
        QCOMPARE(versions(news.releases), (QStringList{QStringLiteral("0.3.0"), QStringLiteral("0.2.1")}));
        QVERIFY(news.commits.isEmpty());
    }

    // A nightly built on 0.2.1 predates the 0.2.1 release, so moving from it
    // to 0.3.0 crosses 0.2.1 too.
    void whatsNewCountsANightlysOwnBaseAsCrossed()
    {
        const WhatsNew news = whatsNew(sampleNotes(), {}, QStringLiteral("0.2.1-nightly.20260901+gaaa1111"),
                                       QStringLiteral("0.3.0"));
        QCOMPARE(versions(news.releases), (QStringList{QStringLiteral("0.3.0"), QStringLiteral("0.2.1")}));
    }

    void whatsNewWithoutAnUpdateShowsTheNewestReleaseNotAhead()
    {
        const WhatsNew news = whatsNew(sampleNotes(), {}, QString(), QStringLiteral("0.2.1"));
        QCOMPARE(versions(news.releases), QStringList{QStringLiteral("0.2.1")});
    }

    void whatsNewBetweenNightliesListsTheCommitsInBetween()
    {
        const QList<HistoryCommit> history{
            {QStringLiteral("ccc3333"), QStringLiteral("Merge pull request #12 from me/tray"), QStringLiteral("Tidy the tray\n\nMore.")},
            {QStringLiteral("bbb2222"), QStringLiteral("Fix the popup (#11)"), QString()},
            {QStringLiteral("aaa1111f"), QStringLiteral("The last nightly"), QString()},
            {QStringLiteral("9990000"), QStringLiteral("Older"), QString()}};
        const WhatsNew news = whatsNew(sampleNotes(), history, QStringLiteral("0.2.1-nightly.20260901+gaaa1111"),
                                       QStringLiteral("0.2.1-nightly.20260902+gccc3333"));
        QCOMPARE(news.commits.size(), 2);
        QCOMPARE(news.commits.at(0).sha, QStringLiteral("ccc3333"));
        QCOMPARE(news.commits.at(1).sha, QStringLiteral("bbb2222"));
        QCOMPARE(news.comparison, QUrl(QStringLiteral("https://github.com/firemonster612/speecher/compare/aaa1111...ccc3333")));
        QVERIFY(news.releases.isEmpty());

        // A nightly older than the embedded history has no commits to list;
        // GitHub's comparison stands in for them.
        const WhatsNew old = whatsNew(sampleNotes(), history, QStringLiteral("0.2.1-nightly.20260801+gfff0000"),
                                      QStringLiteral("0.2.1-nightly.20260902+gccc3333"));
        QVERIFY(old.commits.isEmpty());
        QCOMPARE(old.comparison, QUrl(QStringLiteral("https://github.com/firemonster612/speecher/compare/fff0000...ccc3333")));
        QVERIFY(old.releases.isEmpty());
    }

    void aCommitIsSummedUpByItsPullRequest()
    {
        const CommitSummary merged = commitSummary({QStringLiteral("ccc3333"), QStringLiteral("Merge pull request #12 from me/tray"),
                                                    QStringLiteral("Tidy the tray\n\nMore.")});
        QCOMPARE(merged.title, QStringLiteral("Tidy the tray"));
        QCOMPARE(merged.pullRequest, 12);
        QCOMPARE(merged.url, QUrl(QStringLiteral("https://github.com/firemonster612/speecher/pull/12")));

        const CommitSummary squashed = commitSummary({QStringLiteral("bbb2222"), QStringLiteral("Fix the popup (#11)"), QString()});
        QCOMPARE(squashed.title, QStringLiteral("Fix the popup"));
        QCOMPARE(squashed.pullRequest, 11);
        QCOMPARE(squashed.url, QUrl(QStringLiteral("https://github.com/firemonster612/speecher/pull/11")));

        const CommitSummary pushed = commitSummary({QStringLiteral("aaa1111"), QStringLiteral("Tidy the tray icon"), QString()});
        QCOMPARE(pushed.title, QStringLiteral("Tidy the tray icon"));
        QCOMPARE(pushed.pullRequest, 0);
        QCOMPARE(pushed.url, QUrl(QStringLiteral("https://github.com/firemonster612/speecher/commit/aaa1111")));
    }

    void downgradeDoesNotCreatePendingWhatsNewState()
    {
        {
            SettingsStore existingSettings;
            existingSettings.raw().clear();
            existingSettings.setUpdatesLastRunVersion(QStringLiteral("9.0.0"));
        }

        ApplicationController controller(true);
        QVERIFY(controller.whatsNew()->pendingVersion().isEmpty());
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
        QCOMPARE(controller.whatsNew()->pendingVersion(), previous);
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
