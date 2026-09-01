#include "app/AppImageUpdater.h"
#include "core/SettingsStore.h"
#include "dictation/DictationSession.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QTimer>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <sys/stat.h>

namespace speecher {
namespace {

constexpr qint64 startupCheckIntervalMs = 20LL * 60 * 60 * 1000;
constexpr int dailyCheckIntervalMs = 24 * 60 * 60 * 1000;
const QUrl stableManifestUrl(QStringLiteral(
    "https://github.com/firemonster612/speecher/releases/latest/download/update-manifest.json"));
const QUrl nightlyManifestUrl(QStringLiteral(
    "https://github.com/firemonster612/speecher/releases/download/nightly/update-manifest.json"));

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

bool restartSafe(DictationState state)
{
    return state == DictationState::Idle || state == DictationState::Error;
}

} // namespace

AppImageUpdater::AppImageUpdater(SettingsStore *settings,
                                 DictationSession *session,
                                 QObject *parent)
    : UpdateController(parent)
    , m_settings(settings)
    , m_session(session)
    , m_network(new QNetworkAccessManager(this))
    , m_dailyTimer(new QTimer(this))
{
    m_network->setTransferTimeout(30000);
    m_dismissedVersion = m_settings->updatesDismissedVersion();
    const QString appImage = QString::fromLocal8Bit(qgetenv("APPIMAGE"));
    if (QFileInfo(appImage).isFile()) {
        m_appImagePath = QFileInfo(appImage).absoluteFilePath();
    }

    connect(m_session, &DictationSession::stateChanged, this, [this] {
        if (m_state == State::RestartPending
            && restartSafe(m_session->state())) {
            restartAppImage();
        }
    });
}

AppImageUpdater::~AppImageUpdater()
{
    clearDownload();
}

void AppImageUpdater::start()
{
    m_dailyTimer->setInterval(dailyCheckIntervalMs);
    connect(m_dailyTimer, &QTimer::timeout, this, [this] {
        if (m_settings->autoCheckUpdates()) {
            checkForUpdates(m_settings->updateChannel());
        }
    });
    m_dailyTimer->start();

    QTimer::singleShot(0, this, [this] {
        const qint64 lastCheck = m_settings->updatesLastCheckTime();
        if (m_settings->autoCheckUpdates()
            && QDateTime::currentMSecsSinceEpoch() - lastCheck > startupCheckIntervalMs) {
            checkForUpdates(m_settings->updateChannel());
        }
    });
}

UpdateController::State AppImageUpdater::state() const
{
    return m_state;
}

QString AppImageUpdater::currentVersion() const
{
    return QStringLiteral(SPEECHER_VERSION);
}

qint64 AppImageUpdater::currentBuildNumber() const
{
    return SPEECHER_BUILD_NUMBER;
}

QString AppImageUpdater::availableVersion() const
{
    return m_manifest.version;
}

int AppImageUpdater::downloadPercent() const
{
    return m_downloadPercent;
}

QString AppImageUpdater::errorMessage() const
{
    return m_error;
}

bool AppImageUpdater::isAppImage() const
{
    return !m_appImagePath.isEmpty();
}

bool AppImageUpdater::bannerVisible() const
{
    if (m_state == State::Error) {
        return true;
    }
    if (m_state == State::UpdateAvailable) {
        return m_manifest.version != m_dismissedVersion;
    }
    return m_state == State::Downloading || m_state == State::ReadyToRestart
        || m_state == State::RestartPending;
}

std::optional<UpdateManifest> AppImageUpdater::parseManifest(const QByteArray &json,
                                                              QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, QStringLiteral("The update manifest is not valid JSON."));
        return std::nullopt;
    }

    const QJsonObject root = document.object();
    UpdateManifest manifest;
    manifest.version = root.value(QStringLiteral("version")).toString();
    manifest.buildNumber = root.value(QStringLiteral("buildNumber")).toInteger(-1);
    const QJsonObject linux = root.value(QStringLiteral("linux-x86_64")).toObject();
    manifest.appImageUrl = QUrl(linux.value(QStringLiteral("appimage")).toString());
    manifest.sha256 = linux.value(QStringLiteral("sha256")).toString().toLatin1().toLower();

    const QRegularExpression shaPattern(QStringLiteral("^[0-9a-f]{64}$"));
    if (manifest.version.isEmpty() || manifest.buildNumber < 0
        || !manifest.appImageUrl.isValid()
        || manifest.appImageUrl.scheme() != QStringLiteral("https")
        || !shaPattern.match(QString::fromLatin1(manifest.sha256)).hasMatch()) {
        setError(error, QStringLiteral("The update manifest is missing required release data."));
        return std::nullopt;
    }
    return manifest;
}

bool AppImageUpdater::isNewerBuild(const UpdateManifest &manifest,
                                   qint64 currentBuildNumber)
{
    return manifest.buildNumber > currentBuildNumber;
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
    return AppImageFileIdentity{quint64(status.st_ino),
                                qint64(status.st_mtim.tv_sec),
                                qint64(status.st_mtim.tv_nsec)};
}

bool AppImageUpdater::swapAppImage(const QString &downloadedPath,
                                   const QString &installedPath,
                                   const AppImageFileIdentity &expectedIdentity,
                                   QString *error)
{
    QFile downloaded(downloadedPath);
    QFileDevice::Permissions permissions = downloaded.permissions();
    permissions |= QFileDevice::ExeOwner | QFileDevice::ExeUser
        | QFileDevice::ExeGroup | QFileDevice::ExeOther;
    if (!downloaded.setPermissions(permissions)) {
        setError(error, QStringLiteral("Could not make the downloaded AppImage executable."));
        return false;
    }

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

    std::error_code renameError;
    // The verified download is beside the target, so rename atomically overwrites it.
    std::filesystem::rename(QFile::encodeName(downloadedPath).constData(),
                            QFile::encodeName(installedPath).constData(),
                            renameError);
    if (renameError) {
        setError(error,
                 QStringLiteral("Could not install the new AppImage: %1")
                     .arg(QString::fromStdString(renameError.message())));
        return false;
    }
    return true;
}

void AppImageUpdater::checkForUpdates(UpdateChannel channel)
{
    if (m_state == State::Checking || m_state == State::Downloading
        || m_state == State::ReadyToRestart || m_state == State::RestartPending) {
        return;
    }

    m_manifest = {};
    // The timestamp intentionally records request start, so failed checks still obey the cadence.
    m_settings->setUpdatesLastCheckTime(QDateTime::currentMSecsSinceEpoch());
    setState(State::Checking);
    QNetworkRequest request(manifestUrl(channel));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::finished, this, [this, reply = m_reply] {
        finishCheck(reply);
    });
}

void AppImageUpdater::updateNow()
{
    if (m_state == State::ReadyToRestart) {
        restartNow();
        return;
    }
    if (m_state == State::Error) {
        checkForUpdates(m_settings->updateChannel());
        return;
    }
    if (m_state != State::UpdateAvailable) {
        return;
    }
    if (!isAppImage()) {
        emit openReleasePageRequested();
        return;
    }
    beginDownload();
}

void AppImageUpdater::restartNow()
{
    if (m_state != State::ReadyToRestart) {
        return;
    }
    if (!restartSafe(m_session->state())) {
        setState(State::RestartPending);
        return;
    }
    restartAppImage();
}

void AppImageUpdater::dismissAvailableVersion()
{
    if (m_state == State::Error) {
        setState(State::Idle);
        return;
    }
    if (m_state != State::UpdateAvailable || m_manifest.version.isEmpty()) {
        return;
    }
    m_dismissedVersion = m_manifest.version;
    m_settings->setUpdatesDismissedVersion(m_dismissedVersion);
    emit changed();
}

QUrl AppImageUpdater::manifestUrl(UpdateChannel channel) const
{
    return channel == UpdateChannel::Nightly
        ? nightlyManifestUrl
        : stableManifestUrl;
}

void AppImageUpdater::finishCheck(QNetworkReply *reply)
{
    if (m_reply != reply) {
        return;
    }
    m_reply = nullptr;
    const QByteArray body = reply->readAll();
    const QString networkError = reply->error() == QNetworkReply::NoError
        ? QString()
        : reply->errorString();
    reply->deleteLater();
    if (!networkError.isEmpty()) {
        setState(State::CheckFailed,
                 QStringLiteral("Could not check for updates: %1").arg(networkError));
        return;
    }

    QString error;
    const std::optional<UpdateManifest> manifest = parseManifest(body, &error);
    if (!manifest) {
        setState(State::CheckFailed, error);
        return;
    }
    if (!isNewerBuild(*manifest, currentBuildNumber())) {
        m_manifest = {};
        setState(State::UpToDate);
        return;
    }

    m_manifest = *manifest;
    setState(State::UpdateAvailable);
    if (m_settings->autoInstallUpdates() && isAppImage()) {
        beginDownload();
    }
}

void AppImageUpdater::beginDownload()
{
    QString identityError;
    m_appImageIdentity = fileIdentity(m_appImagePath, &identityError);
    if (!m_appImageIdentity) {
        setState(State::Error, identityError);
        return;
    }
    m_download = new QTemporaryFile(
        QFileInfo(m_appImagePath).absolutePath()
        + QStringLiteral("/.speecher-update-XXXXXX.AppImage"));
    m_download->setAutoRemove(false);
    if (!m_download->open()) {
        clearDownload();
        setState(State::Error, QStringLiteral("Could not create the AppImage update file."));
        return;
    }

    m_downloadPercent = 0;
    m_downloadError.clear();
    setState(State::Downloading);
    QNetworkRequest request(m_manifest.appImageUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, &AppImageUpdater::writeDownloadedData);
    connect(m_reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                if (total <= 0) {
                    return;
                }
                const int percent = int(received * 100 / total);
                if (percent != m_downloadPercent) {
                    m_downloadPercent = percent;
                    emit changed();
                }
            });
    connect(m_reply, &QNetworkReply::finished, this, [this, reply = m_reply] {
        finishDownload(reply);
    });
}

void AppImageUpdater::writeDownloadedData()
{
    if (!m_reply || !m_download || !m_downloadError.isEmpty()) {
        return;
    }
    const QByteArray data = m_reply->readAll();
    if (m_download->write(data) != data.size()) {
        m_downloadError = QStringLiteral("Could not write the AppImage update file.");
        m_reply->abort();
    }
}

void AppImageUpdater::finishDownload(QNetworkReply *reply)
{
    if (m_reply != reply) {
        return;
    }
    writeDownloadedData();
    m_reply = nullptr;
    const QString networkError = reply->error() == QNetworkReply::NoError
        ? QString()
        : reply->errorString();
    reply->deleteLater();
    if (!m_downloadError.isEmpty() || !networkError.isEmpty()) {
        const QString error = m_downloadError.isEmpty()
            ? QStringLiteral("Could not download the AppImage: %1").arg(networkError)
            : m_downloadError;
        clearDownload();
        setState(State::Error, error);
        return;
    }

    m_download->flush();
    m_download->close();
    const QString downloadedPath = m_download->fileName();
    delete m_download;
    m_download = nullptr;

    QString error;
    if (!verifyDownload(downloadedPath, m_manifest.sha256, &error)
        || !swapAppImage(downloadedPath, m_appImagePath, *m_appImageIdentity, &error)) {
        QFile::remove(downloadedPath);
        setState(State::Error, error);
        return;
    }
    m_downloadPercent = 100;
    setState(State::ReadyToRestart);
}

bool AppImageUpdater::verifyDownload(const QString &path,
                                     const QByteArray &expectedSha,
                                     QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("Could not read the downloaded AppImage."));
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file) || hash.result().toHex() != expectedSha) {
        setError(error, QStringLiteral("The downloaded AppImage did not match its SHA-256 checksum."));
        return false;
    }
    return true;
}

void AppImageUpdater::clearDownload()
{
    if (!m_download) {
        return;
    }
    const QString path = m_download->fileName();
    m_download->close();
    delete m_download;
    m_download = nullptr;
    QFile::remove(path);
}

void AppImageUpdater::setState(State state, const QString &error)
{
    m_state = state;
    m_error = error;
    emit changed();
}

void AppImageUpdater::restartAppImage()
{
    if (!isAppImage()) {
        emit openReleasePageRequested();
        return;
    }
    if (!QProcess::startDetached(m_appImagePath,
                                 {},
                                 QFileInfo(m_appImagePath).absolutePath())) {
        setState(State::ReadyToRestart, QStringLiteral("Could not restart Speecher."));
        return;
    }
    QCoreApplication::quit();
}

} // namespace speecher
