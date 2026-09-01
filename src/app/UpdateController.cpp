#include "app/UpdateController.h"

#include "core/SettingsStore.h"
#include "core/settings/SettingsKeys.h"
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
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimer>

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

} // namespace

UpdateController::UpdateController(SettingsStore *settings,
                                   DictationSession *session,
                                   QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_session(session)
    , m_network(new QNetworkAccessManager(this))
    , m_dailyTimer(new QTimer(this))
{
    const QString appImage = QString::fromLocal8Bit(qgetenv("APPIMAGE"));
    if (QFileInfo(appImage).isFile()) {
        m_appImagePath = QFileInfo(appImage).absoluteFilePath();
    }

    connect(m_session, &DictationSession::stateChanged, this, [this] {
        if (m_restartPending && !dictationSessionActive()) {
            m_restartPending = false;
            restartAppImage();
        }
    });
    if (QStandardPaths::isTestModeEnabled()) {
        return;
    }

    m_dailyTimer->setInterval(dailyCheckIntervalMs);
    connect(m_dailyTimer, &QTimer::timeout, this, [this] {
        if (m_settings->autoCheckUpdates()) {
            checkForUpdates();
        }
    });
    m_dailyTimer->start();

    QTimer::singleShot(0, this, [this] {
        const qint64 lastCheck = m_settings->raw()
                                     .value(SettingsKeys::UpdatesLastCheckTime, 0)
                                     .toLongLong();
        if (m_settings->autoCheckUpdates()
            && QDateTime::currentMSecsSinceEpoch() - lastCheck > startupCheckIntervalMs) {
            checkForUpdates();
        }
    });
}

UpdateController::~UpdateController()
{
    clearDownload();
}

UpdateController::State UpdateController::state() const
{
    return m_state;
}

QString UpdateController::currentVersion() const
{
    return QStringLiteral(SPEECHER_VERSION);
}

qint64 UpdateController::currentBuildNumber() const
{
    return SPEECHER_BUILD_NUMBER;
}

QString UpdateController::availableVersion() const
{
    return m_manifest.version;
}

int UpdateController::downloadPercent() const
{
    return m_downloadPercent;
}

QString UpdateController::errorMessage() const
{
    return m_error;
}

bool UpdateController::isAppImage() const
{
    return !m_appImagePath.isEmpty();
}

bool UpdateController::bannerVisible() const
{
    if (m_manifest.version.isEmpty()) {
        return false;
    }
    const bool updateState = m_state == State::UpdateAvailable
        || m_state == State::Downloading
        || m_state == State::ReadyToRestart;
    return updateState
        && m_settings->raw().value(SettingsKeys::UpdatesDismissedVersion).toString()
               != m_manifest.version;
}

std::optional<UpdateManifest> UpdateController::parseManifest(const QByteArray &json,
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

bool UpdateController::isNewerBuild(const UpdateManifest &manifest,
                                    qint64 currentBuildNumber)
{
    return manifest.buildNumber > currentBuildNumber;
}

bool UpdateController::swapAppImage(const QString &downloadedPath,
                                    const QString &installedPath,
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

    QTemporaryFile oldTemplate(installedPath + QStringLiteral(".old-XXXXXX"));
    oldTemplate.setAutoRemove(false);
    if (!oldTemplate.open()) {
        setError(error, QStringLiteral("Could not reserve a backup name for the current AppImage."));
        return false;
    }
    const QString oldPath = oldTemplate.fileName();
    oldTemplate.close();
    if (!QFile::remove(oldPath) || !QFile::rename(installedPath, oldPath)) {
        setError(error, QStringLiteral("Could not move the current AppImage aside."));
        return false;
    }

    if (!QFile::rename(downloadedPath, installedPath)) {
        if (!QFile::rename(oldPath, installedPath)) {
            setError(error, QStringLiteral(
                                "Could not install the new AppImage or restore the current one."));
        } else {
            setError(error, QStringLiteral("Could not install the new AppImage."));
        }
        return false;
    }
    if (!QFile::remove(oldPath)) {
        setError(error, QStringLiteral("The new AppImage is installed, but the old file could not be removed."));
        return false;
    }
    return true;
}

void UpdateController::checkForUpdates()
{
    if (m_state == State::Checking || m_state == State::Downloading
        || m_state == State::ReadyToRestart) {
        return;
    }

    m_manifest = {};
    m_settings->raw().setValue(SettingsKeys::UpdatesLastCheckTime,
                               QDateTime::currentMSecsSinceEpoch());
    setState(State::Checking);
    QNetworkRequest request(manifestUrl());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::finished, this, [this, reply = m_reply] {
        finishCheck(reply);
    });
}

void UpdateController::updateNow()
{
    if (m_state == State::ReadyToRestart) {
        restartNow();
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

void UpdateController::restartNow()
{
    if (m_state != State::ReadyToRestart) {
        return;
    }
    if (dictationSessionActive()) {
        m_restartPending = true;
        return;
    }
    restartAppImage();
}

void UpdateController::dismissAvailableVersion()
{
    if (m_manifest.version.isEmpty()) {
        return;
    }
    m_settings->raw().setValue(SettingsKeys::UpdatesDismissedVersion,
                               m_manifest.version);
    emit changed();
}

QUrl UpdateController::manifestUrl() const
{
    return m_settings->updateChannel() == UpdateChannel::Nightly
        ? nightlyManifestUrl
        : stableManifestUrl;
}

void UpdateController::finishCheck(QNetworkReply *reply)
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
        setState(State::Error, QStringLiteral("Could not check for updates: %1").arg(networkError));
        return;
    }

    QString error;
    const std::optional<UpdateManifest> manifest = parseManifest(body, &error);
    if (!manifest) {
        setState(State::Error, error);
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

void UpdateController::beginDownload()
{
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
    connect(m_reply, &QNetworkReply::readyRead, this, &UpdateController::writeDownloadedData);
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

void UpdateController::writeDownloadedData()
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

void UpdateController::finishDownload(QNetworkReply *reply)
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
    if (!verifyDownload(downloadedPath, &error)
        || !swapAppImage(downloadedPath, m_appImagePath, &error)) {
        QFile::remove(downloadedPath);
        setState(State::Error, error);
        return;
    }
    m_downloadPercent = 100;
    setState(State::ReadyToRestart);
}

bool UpdateController::verifyDownload(const QString &path, QString *error) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("Could not read the downloaded AppImage."));
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file) || hash.result().toHex() != m_manifest.sha256) {
        setError(error, QStringLiteral("The downloaded AppImage did not match its SHA-256 checksum."));
        return false;
    }
    return true;
}

void UpdateController::clearDownload()
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

void UpdateController::setState(State state, const QString &error)
{
    m_state = state;
    m_error = error;
    emit changed();
}

bool UpdateController::dictationSessionActive() const
{
    const DictationState state = m_session->state();
    return state != DictationState::Idle && state != DictationState::Error;
}

void UpdateController::restartAppImage()
{
    if (!isAppImage()) {
        emit openReleasePageRequested();
        return;
    }
    if (!QProcess::startDetached(m_appImagePath,
                                 {},
                                 QFileInfo(m_appImagePath).absolutePath())) {
        setState(State::Error, QStringLiteral("Could not restart Speecher."));
        return;
    }
    QCoreApplication::quit();
}

} // namespace speecher
