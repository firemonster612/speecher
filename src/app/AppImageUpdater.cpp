#include "app/AppImageUpdater.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QTemporaryFile>
#include <QTimer>
#include <QUuid>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace speecher {
namespace {

constexpr int restartHandshakeTimeoutMs = 15000;
constexpr auto restartSocketEnvironment = "SPEECHER_RESTART_SOCKET";

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

bool synchronizePath(const QString &path,
                     int flags,
                     const QString &description,
                     QString *error)
{
    const QByteArray encodedPath = QFile::encodeName(path);
    const int descriptor = ::open(encodedPath.constData(), flags);
    if (descriptor < 0) {
        setError(error,
                 QStringLiteral("Could not open the %1 for synchronization: %2")
                     .arg(description,
                          QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }
    if (::fsync(descriptor) != 0) {
        const QString cause = QString::fromLocal8Bit(std::strerror(errno));
        ::close(descriptor);
        setError(error,
                 QStringLiteral("Could not synchronize the %1: %2")
                     .arg(description, cause));
        return false;
    }
    ::close(descriptor);
    return true;
}

} // namespace

AppImageUpdater::AppImageUpdater(SettingsStore *settings,
                                 DictationSession *session,
                                 QObject *parent)
    : ManifestUpdater(settings,
                      session,
                      QStringLiteral("linux-x86_64"),
                      QStringLiteral("appimage"),
                      QStringLiteral("AppImage"),
                      parent)
{
    const QString appImage = QString::fromLocal8Bit(qgetenv("APPIMAGE"));
    if (QFileInfo(appImage).isFile()) {
        m_appImagePath = QFileInfo(appImage).absoluteFilePath();
    }
}

bool AppImageUpdater::isAppImage() const
{
    return !m_appImagePath.isEmpty();
}

bool AppImageUpdater::supportsAutomaticDownloads() const
{
    return isAppImage();
}

void AppImageUpdater::waitForRestartParent()
{
    const QString socketName = qEnvironmentVariable(restartSocketEnvironment);
    if (socketName.isEmpty()) {
        return;
    }
    qunsetenv(restartSocketEnvironment);

    QLocalSocket socket;
    socket.connectToServer(socketName);
    if (socket.waitForConnected(restartHandshakeTimeoutMs)) {
        socket.waitForDisconnected(restartHandshakeTimeoutMs);
    }
}

std::optional<AppImageFileIdentity> AppImageUpdater::fileIdentity(const QString &path,
                                                                  QString *error)
{
    struct stat status {};
    const QByteArray encodedPath = QFile::encodeName(path);
    if (::stat(encodedPath.constData(), &status) != 0) {
        setError(error,
                 QStringLiteral("Could not inspect the installed AppImage: %1")
                     .arg(QString::fromLocal8Bit(std::strerror(errno))));
        return std::nullopt;
    }
#ifdef __APPLE__
    const auto &modified = status.st_mtimespec;
#else
    const auto &modified = status.st_mtim;
#endif
    return AppImageFileIdentity{quint64(status.st_ino),
                                qint64(modified.tv_sec),
                                qint64(modified.tv_nsec)};
}

bool AppImageUpdater::swapAppImage(const QString &downloadedPath,
                                   const QString &installedPath,
                                   const AppImageFileIdentity &expectedIdentity,
                                   QString *error)
{
    const std::optional<AppImageFileIdentity> currentIdentity =
        fileIdentity(installedPath, error);
    if (!currentIdentity || *currentIdentity != expectedIdentity) {
        if (currentIdentity) {
            setError(error, QStringLiteral(
                                "The installed AppImage changed since the download started; "
                                "the update was not installed."));
        }
        return false;
    }

    QFile downloaded(downloadedPath);
    QFileDevice::Permissions permissions = QFile::permissions(installedPath);
    permissions |= QFileDevice::ExeOwner;
    if (!downloaded.setPermissions(permissions)) {
        setError(error, QStringLiteral("Could not preserve the installed AppImage permissions."));
        return false;
    }
    if (!synchronizePath(downloadedPath,
                         O_RDONLY | O_CLOEXEC | O_NONBLOCK,
                         QStringLiteral("downloaded AppImage"),
                         error)) {
        return false;
    }

    std::error_code renameError;
    std::filesystem::rename(QFile::encodeName(downloadedPath).constData(),
                            QFile::encodeName(installedPath).constData(),
                            renameError);
    if (renameError) {
        setError(error,
                 QStringLiteral("Could not install the new AppImage: %1")
                     .arg(QString::fromStdString(renameError.message())));
        return false;
    }
    if (!synchronizePath(QFileInfo(installedPath).absolutePath(),
                         O_RDONLY | O_CLOEXEC | O_DIRECTORY,
                         QStringLiteral("AppImage folder"),
                         error)) {
        return false;
    }
    return true;
}

std::unique_ptr<QFile> AppImageUpdater::createDownload(QString *error,
                                                        bool *manualInstallRequired)
{
    const QString folder = QFileInfo(m_appImagePath).absolutePath();
    if (!QFileInfo(folder).isWritable()) {
        *manualInstallRequired = true;
        setError(error,
                 QStringLiteral("The AppImage folder is not writable. Download the replacement "
                                "from the release page."));
        return {};
    }

    m_appImageIdentity = fileIdentity(m_appImagePath, error);
    if (!m_appImageIdentity) {
        return {};
    }
    auto download = std::make_unique<QTemporaryFile>(
        folder + QStringLiteral("/.speecher-update-XXXXXX.AppImage"));
    if (!download->open()) {
        setError(error, QStringLiteral("Could not create the AppImage update file."));
        return {};
    }
    download->setAutoRemove(false);
    return download;
}

bool AppImageUpdater::installDownload(const QString &path, QString *error)
{
    return swapAppImage(path, m_appImagePath, *m_appImageIdentity, error);
}

QProcessEnvironment AppImageUpdater::restartEnvironment(
    const QStringList &arguments,
    QProcessEnvironment environment)
{
    if (environment.contains(QStringLiteral("APPIMAGE_EXTRACT_AND_RUN"))
        || arguments.contains(QStringLiteral("--appimage-extract-and-run"))) {
        environment.insert(QStringLiteral("APPIMAGE_EXTRACT_AND_RUN"),
                           QStringLiteral("1"));
    }
    return environment;
}

void AppImageUpdater::restartApplication()
{
    if (m_restartServer) {
        return;
    }
    if (!isAppImage()) {
        emit openReleasePageRequested();
        return;
    }
    m_restartServer = new QLocalServer(this);
    const QString socketName = QStringLiteral("speecher-restart-%1").arg(
        QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!m_restartServer->listen(socketName)) {
        delete m_restartServer;
        m_restartServer = nullptr;
        setState(State::ReadyToRestart,
                 QStringLiteral("Could not prepare to restart Speecher."));
        return;
    }
    connect(m_restartServer, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = m_restartServer->nextPendingConnection()) {
            socket->setParent(m_restartServer);
        }
        QCoreApplication::quit();
    });

    QProcess process;
    process.setProgram(m_appImagePath);
    process.setArguments(QCoreApplication::arguments().mid(1));
    process.setWorkingDirectory(QFileInfo(m_appImagePath).absolutePath());
    QProcessEnvironment environment = restartEnvironment(
        QCoreApplication::arguments(), QProcessEnvironment::systemEnvironment());
    environment.insert(QString::fromLatin1(restartSocketEnvironment), socketName);
    process.setProcessEnvironment(environment);
    if (!process.startDetached()) {
        delete m_restartServer;
        m_restartServer = nullptr;
        setState(State::ReadyToRestart, QStringLiteral("Could not restart Speecher."));
        return;
    }
    setState(State::Restarting);
    QTimer::singleShot(restartHandshakeTimeoutMs, this, [this] {
        if (!m_restartServer) {
            return;
        }
        delete m_restartServer;
        m_restartServer = nullptr;
        setState(State::ReadyToRestart,
                 QStringLiteral("The updated AppImage did not finish starting."));
    });
}

} // namespace speecher
