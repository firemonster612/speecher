#include "common/test_suites.h"

#include "app/UpdateController.h"

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

} // namespace

class UpdateControllerTests : public QObject {
    Q_OBJECT

private slots:
    void parsesManifestAndComparesBuildNumbers()
    {
        const QByteArray json = R"json({
            "version": "0.1.1-nightly.20260901+gabc1234",
            "buildNumber": 123,
            "linux-x86_64": {
                "appimage": "https://github.com/firemonster612/speecher/releases/download/nightly/Speecher-x86_64.AppImage",
                "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
            }
        })json";

        QString error;
        const std::optional<UpdateManifest> manifest = UpdateController::parseManifest(json, &error);

        QVERIFY2(manifest.has_value(), qPrintable(error));
        QCOMPARE(manifest->version, QStringLiteral("0.1.1-nightly.20260901+gabc1234"));
        QCOMPARE(manifest->buildNumber, 123);
        QCOMPARE(manifest->appImageUrl.toString(),
                 QStringLiteral("https://github.com/firemonster612/speecher/releases/download/nightly/Speecher-x86_64.AppImage"));
        QCOMPARE(manifest->sha256,
                 QByteArrayLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
        QVERIFY(UpdateController::isNewerBuild(*manifest, 122));
        QVERIFY(!UpdateController::isNewerBuild(*manifest, 123));
        QVERIFY(!UpdateController::isNewerBuild(*manifest, 124));
    }

    void swapsAppImageAndMakesReplacementExecutable()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString installedPath = directory.filePath(QStringLiteral("Speecher.AppImage"));
        const QString downloadedPath = directory.filePath(QStringLiteral("downloaded.AppImage"));

        QFile installed(installedPath);
        QVERIFY(installed.open(QIODevice::WriteOnly));
        QCOMPARE(installed.write("old AppImage"), 12);
        installed.close();

        QFile downloaded(downloadedPath);
        QVERIFY(downloaded.open(QIODevice::WriteOnly));
        QCOMPARE(downloaded.write("new AppImage"), 12);
        downloaded.close();

        QString error;
        QVERIFY2(UpdateController::swapAppImage(downloadedPath, installedPath, &error),
                 qPrintable(error));
        QCOMPARE(readFile(installedPath), QByteArrayLiteral("new AppImage"));
        QVERIFY(QFileInfo(installedPath).isExecutable());
        QVERIFY(!QFileInfo::exists(downloadedPath));
        QCOMPARE(QDir(directory.path()).entryList({QStringLiteral("*.old-*")}, QDir::Files),
                 QStringList());
    }
};

int runUpdateControllerTests(int argc, char **argv)
{
    UpdateControllerTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_update_controller.moc"
