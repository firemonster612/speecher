#include "common/test_suites.h"

#include "app/AppImageUpdater.h"

#include <QFile>
#include <QTemporaryDir>

using namespace speecher;

namespace {

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
    void parsesManifestAndComparesBuildNumbers()
    {
        QString error;
        const std::optional<UpdateManifest> manifest =
            AppImageUpdater::parseManifest(validManifestJson(), &error);

        QVERIFY2(manifest.has_value(), qPrintable(error));
        QCOMPARE(manifest->version, QStringLiteral("0.1.1-nightly.20260901+gabc1234"));
        QCOMPARE(manifest->buildNumber, 123);
        QCOMPARE(manifest->appImageUrl.toString(),
                 QStringLiteral("https://github.com/firemonster612/speecher/releases/download/nightly/Speecher-x86_64.AppImage"));
        QCOMPARE(manifest->sha256,
                 QByteArrayLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
        QVERIFY(AppImageUpdater::isNewerBuild(*manifest, 122));
        QVERIFY(!AppImageUpdater::isNewerBuild(*manifest, 123));
        QVERIFY(!AppImageUpdater::isNewerBuild(*manifest, 124));
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
        QVERIFY(!AppImageUpdater::parseManifest(json).has_value());
    }

    void rejectsDownloadWithMismatchedChecksum()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("downloaded.AppImage"));
        writeFile(path, QByteArrayLiteral("new AppImage"));

        QString error;
        QVERIFY(!AppImageUpdater::verifyDownload(
            path,
            QByteArrayLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"),
            &error));
        QVERIFY(error.contains(QStringLiteral("SHA-256")));
    }

    void swapsAppImageAndMakesReplacementExecutable()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString installedPath = directory.filePath(QStringLiteral("Speecher.AppImage"));
        const QString downloadedPath = directory.filePath(QStringLiteral("downloaded.AppImage"));

        writeFile(installedPath, QByteArrayLiteral("old AppImage"));
        writeFile(downloadedPath, QByteArrayLiteral("new AppImage"));

        QString error;
        const std::optional<AppImageFileIdentity> identity =
            AppImageUpdater::fileIdentity(installedPath, &error);
        QVERIFY2(identity.has_value(), qPrintable(error));
        QVERIFY2(AppImageUpdater::swapAppImage(
                     downloadedPath, installedPath, *identity, &error),
                 qPrintable(error));
        QCOMPARE(readFile(installedPath), QByteArrayLiteral("new AppImage"));
        QVERIFY(QFileInfo(installedPath).isExecutable());
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
};

int runUpdateControllerTests(int argc, char **argv)
{
    UpdateControllerTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_update_controller.moc"
