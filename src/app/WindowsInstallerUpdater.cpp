#include "app/WindowsInstallerUpdater.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

namespace speecher {
namespace {

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

} // namespace

WindowsInstallerUpdater::WindowsInstallerUpdater(SettingsStore *settings,
                                                 DictationSession *session,
                                                 QObject *parent)
    : ManifestUpdater(settings,
                      session,
                      QStringLiteral("windows-x86_64"),
                      QStringLiteral("installer"),
                      QStringLiteral("installer"),
                      parent)
{
}

bool WindowsInstallerUpdater::isAppImage() const
{
    return false;
}

bool WindowsInstallerUpdater::supportsAutomaticDownloads() const
{
    return true;
}

std::unique_ptr<QFile> WindowsInstallerUpdater::createDownload(
    QString *error,
    bool *manualInstallRequired)
{
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    const QString folder = QDir(localAppData).filePath(QStringLiteral("Speecher/updates"));
    if (localAppData.isEmpty()
        || !QDir().mkpath(folder)
        || !QFileInfo(folder).isWritable()) {
        *manualInstallRequired = true;
        setError(error,
                 QStringLiteral("The updates folder is not writable. Download the installer "
                                "from the release page."));
        return {};
    }

    QDir directory(folder);
    for (const QString &oldInstaller : directory.entryList(
             {QStringLiteral("Speecher-Setup-*.exe")}, QDir::Files)) {
        directory.remove(oldInstaller);
    }
    const QString path = directory.filePath(
        QStringLiteral("Speecher-Setup-%1.exe").arg(manifest().version));
    auto download = std::make_unique<QFile>(path);
    if (!download->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *manualInstallRequired = true;
        setError(error,
                 QStringLiteral("The updates folder is not writable. Download the installer "
                                "from the release page."));
        return {};
    }
    return download;
}

bool WindowsInstallerUpdater::installDownload(const QString &path, QString *)
{
    m_installerPath = path;
    return true;
}

void WindowsInstallerUpdater::restartApplication()
{
    const QString logPath = QFileInfo(m_installerPath).dir().filePath(
        QStringLiteral("speecher-update.log"));
    QStringList arguments{
        QStringLiteral("/VERYSILENT"),
        QStringLiteral("/SUPPRESSMSGBOXES"),
        QStringLiteral("/NORESTART"),
        QStringLiteral("/CLOSEAPPLICATIONS"),
        QStringLiteral("/FORCECLOSEAPPLICATIONS"),
        QStringLiteral("/LOG=%1").arg(QDir::toNativeSeparators(logPath)),
    };
    const QStringList restartArguments = QCoreApplication::arguments().mid(1);
    arguments.append(QStringLiteral("/RESTARTARGCOUNT=%1").arg(restartArguments.size()));
    for (qsizetype index = 0; index < restartArguments.size(); ++index) {
        arguments.append(QStringLiteral("/RESTARTARG%1=%2")
                             .arg(index)
                             .arg(restartArguments.at(index)));
    }
    if (!QProcess::startDetached(m_installerPath, arguments)) {
        setState(State::ReadyToRestart,
                 QStringLiteral("Could not start the Speecher installer."));
        return;
    }
    setState(State::Restarting);
    QCoreApplication::quit();
}

} // namespace speecher
