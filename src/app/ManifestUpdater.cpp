#include "app/ManifestUpdater.h"

#include "core/SettingsStore.h"
#include "dictation/DictationSession.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>

#include <utility>

namespace speecher {
namespace {

constexpr qint64 startupCheckIntervalMs = 20LL * 60 * 60 * 1000;
constexpr int dailyCheckIntervalMs = 24 * 60 * 60 * 1000;
constexpr int initialRetryIntervalMs = 5 * 60 * 1000;
constexpr int maximumRetryIntervalMs = 60 * 60 * 1000;
constexpr int visibleAutomaticFailureCount = 3;
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

int automaticRetryInterval(int failureCount)
{
    int interval = initialRetryIntervalMs;
    for (int failure = 1;
         failure < failureCount && interval < maximumRetryIntervalMs;
         ++failure) {
        interval = qMin(interval * 2, maximumRetryIntervalMs);
    }
    return interval;
}

} // namespace

ManifestUpdater::ManifestUpdater(SettingsStore *settings,
                                 DictationSession *session,
                                 QString platformKey,
                                 QString downloadKey,
                                 QString downloadDescription,
                                 QObject *parent)
    : UpdateController(parent)
    , m_settings(settings)
    , m_session(session)
    , m_network(new QNetworkAccessManager(this))
    , m_dailyTimer(new QTimer(this))
    , m_platformKey(std::move(platformKey))
    , m_downloadKey(std::move(downloadKey))
    , m_downloadDescription(std::move(downloadDescription))
    , m_checkChannel(settings->updateChannel())
    , m_selectedChannel(settings->updateChannel())
{
    m_network->setTransferTimeout(30000);
    m_dismissedVersion = m_settings->updatesDismissedVersion();

    connect(m_session, &DictationSession::stateChanged, this, [this] {
        if (m_state == State::RestartPending && restartSafe(m_session->state())) {
            restartApplication();
        }
    });
    connect(m_settings,
            &SettingsStore::updateSettingsChanged,
            this,
            &ManifestUpdater::updateSettingsChanged);
}

ManifestUpdater::~ManifestUpdater()
{
    clearDownload();
}

void ManifestUpdater::start()
{
    m_dailyTimer->setInterval(dailyCheckIntervalMs);
    connect(m_dailyTimer, &QTimer::timeout, this, [this] {
        if (m_settings->autoCheckUpdates()) {
            beginCheck(m_settings->updateChannel(), true);
        }
    });
    m_dailyTimer->start();

    QTimer::singleShot(0, this, [this] {
        const qint64 lastCheck = m_settings->updatesLastCheckTime();
        if (m_settings->autoCheckUpdates()
            && QDateTime::currentMSecsSinceEpoch() - lastCheck > startupCheckIntervalMs) {
            beginCheck(m_settings->updateChannel(), true);
        }
    });
}

UpdateController::State ManifestUpdater::state() const
{
    return m_state;
}

QString ManifestUpdater::currentVersion() const
{
    return QStringLiteral(SPEECHER_VERSION);
}

qint64 ManifestUpdater::currentBuildNumber() const
{
    return SPEECHER_BUILD_NUMBER;
}

QString ManifestUpdater::availableVersion() const
{
    return m_manifest.version;
}

int ManifestUpdater::downloadPercent() const
{
    return m_downloadPercent;
}

QString ManifestUpdater::errorMessage() const
{
    return m_error;
}

bool ManifestUpdater::bannerVisible() const
{
    if (m_state == State::Error) {
        return true;
    }
    if (m_state == State::UpdateAvailable) {
        return m_manifest.version != m_dismissedVersion;
    }
    return m_state == State::Downloading || m_state == State::ReadyToRestart
        || m_state == State::RestartPending || m_state == State::Restarting;
}

bool ManifestUpdater::repeatedAutomaticCheckFailure() const
{
    return m_automaticCheckFailures >= visibleAutomaticFailureCount;
}

bool ManifestUpdater::manualInstallRequired() const
{
    return m_manualInstallRequired;
}

bool ManifestUpdater::stableReplacementAvailable() const
{
    return m_manifest.channel == UpdateChannel::Stable
        && currentVersion().contains(QStringLiteral("-nightly"));
}

std::optional<UpdateManifest> ManifestUpdater::parseManifest(
    const QByteArray &json,
    const QString &platformKey,
    const QString &downloadKey,
    QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, QStringLiteral("The update manifest is not valid JSON."));
        return std::nullopt;
    }

    const QJsonObject root = document.object();
    const QJsonObject platform = root.value(platformKey).toObject();
    UpdateManifest manifest;
    manifest.version = root.value(QStringLiteral("version")).toString();
    manifest.buildNumber = root.value(QStringLiteral("buildNumber")).toInteger(-1);
    manifest.downloadUrl = QUrl(platform.value(downloadKey).toString());
    manifest.sha256 = platform.value(QStringLiteral("sha256")).toString().toLatin1().toLower();

    const QRegularExpression shaPattern(QStringLiteral("^[0-9a-f]{64}$"));
    if (manifest.version.isEmpty() || manifest.buildNumber < 0
        || !manifest.downloadUrl.isValid()
        || manifest.downloadUrl.scheme() != QStringLiteral("https")
        || !shaPattern.match(QString::fromLatin1(manifest.sha256)).hasMatch()) {
        setError(error, QStringLiteral("The update manifest is missing required release data."));
        return std::nullopt;
    }
    return manifest;
}

bool ManifestUpdater::isNewerBuild(const UpdateManifest &manifest,
                                   qint64 currentBuildNumber)
{
    return manifest.buildNumber > currentBuildNumber;
}

bool ManifestUpdater::verifyDownload(const QString &path,
                                     const QByteArray &expectedSha,
                                     const QString &description,
                                     QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("Could not read the downloaded %1.").arg(description));
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file) || hash.result().toHex() != expectedSha) {
        setError(error,
                 QStringLiteral("The downloaded %1 did not match its SHA-256 checksum.")
                     .arg(description));
        return false;
    }
    return true;
}

bool ManifestUpdater::shouldOfferManifest(const UpdateManifest &manifest,
                                          qint64 currentBuildNumber,
                                          const QString &currentVersion,
                                          UpdateChannel channel,
                                          bool automaticCheck)
{
    return isNewerBuild(manifest, currentBuildNumber)
        || (!automaticCheck
            && channel == UpdateChannel::Stable
            && currentVersion.contains(QStringLiteral("-nightly")));
}

void ManifestUpdater::checkForUpdates(UpdateChannel channel)
{
    beginCheck(channel, false);
}

void ManifestUpdater::beginCheck(UpdateChannel channel, bool automaticCheck)
{
    if (m_state == State::Checking || m_state == State::Downloading
        || m_state == State::ReadyToRestart || m_state == State::RestartPending
        || m_state == State::Restarting) {
        return;
    }

    m_manifest = {};
    m_manualInstallRequired = false;
    m_checkChannel = channel;
    m_automaticCheck = automaticCheck;
    setState(State::Checking);
    QNetworkRequest request(manifestUrl(channel));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::finished, this, [this, reply = m_reply] {
        finishCheck(reply);
    });
}

void ManifestUpdater::updateNow()
{
    if (m_state == State::ReadyToRestart) {
        restartNow();
        return;
    }
    if (m_state == State::Error || m_state == State::CheckFailed) {
        if (m_manualInstallRequired) {
            emit openReleasePageRequested();
            return;
        }
        checkForUpdates(m_settings->updateChannel());
        return;
    }
    if (m_state != State::UpdateAvailable) {
        return;
    }
    if (!supportsAutomaticDownloads()) {
        emit openReleasePageRequested();
        return;
    }
    beginDownload(true);
}

void ManifestUpdater::restartNow()
{
    if (m_state != State::ReadyToRestart) {
        return;
    }
    if (!restartSafe(m_session->state())) {
        setState(State::RestartPending);
        return;
    }
    restartApplication();
}

void ManifestUpdater::dismissAvailableVersion()
{
    if (m_state == State::Error) {
        m_manualInstallRequired = false;
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

const UpdateManifest &ManifestUpdater::manifest() const
{
    return m_manifest;
}

QUrl ManifestUpdater::manifestUrl(UpdateChannel channel) const
{
    return channel == UpdateChannel::Nightly ? nightlyManifestUrl : stableManifestUrl;
}

void ManifestUpdater::finishCheck(QNetworkReply *reply)
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
        recordAutomaticCheckFailure();
        setState(State::CheckFailed,
                 QStringLiteral("Could not check for updates: %1").arg(networkError));
        return;
    }

    QString error;
    const std::optional<UpdateManifest> parsed =
        parseManifest(body, m_platformKey, m_downloadKey, &error);
    if (!parsed) {
        recordAutomaticCheckFailure();
        setState(State::CheckFailed, error);
        return;
    }
    m_settings->setUpdatesLastCheckTime(QDateTime::currentMSecsSinceEpoch());
    m_automaticCheckFailures = 0;
    m_dailyTimer->setInterval(dailyCheckIntervalMs);
    if (!shouldOfferManifest(*parsed,
                             currentBuildNumber(),
                             currentVersion(),
                             m_checkChannel,
                             m_automaticCheck)) {
        m_manifest = {};
        setState(State::UpToDate);
        return;
    }

    m_manifest = *parsed;
    m_manifest.channel = m_checkChannel;
    setState(State::UpdateAvailable);
    const bool stableReplacement = m_checkChannel == UpdateChannel::Stable
        && currentVersion().contains(QStringLiteral("-nightly"));
    if (m_settings->autoInstallUpdates()
        && supportsAutomaticDownloads()
        && !stableReplacement) {
        beginDownload(false);
    }
}

void ManifestUpdater::recordAutomaticCheckFailure()
{
    if (!m_automaticCheck) {
        return;
    }
    ++m_automaticCheckFailures;
    m_dailyTimer->setInterval(automaticRetryInterval(m_automaticCheckFailures));
}

void ManifestUpdater::updateSettingsChanged()
{
    const UpdateChannel channel = m_settings->updateChannel();
    if (channel == m_selectedChannel) {
        return;
    }
    m_selectedChannel = channel;
    if (m_state == State::ReadyToRestart || m_state == State::RestartPending
        || m_state == State::Restarting) {
        return;
    }

    if (m_reply) {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        QObject::disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    clearDownload();
    m_manifest = {};
    m_downloadError.clear();
    m_downloadPercent = 0;
    m_manualInstallRequired = false;
    setState(State::Idle);
}

void ManifestUpdater::beginDownload(bool openReleaseOnManualFailure)
{
    QString error;
    bool manualInstallRequired = false;
    m_download = createDownload(&error, &manualInstallRequired);
    if (!m_download) {
        m_manualInstallRequired = manualInstallRequired;
        setState(State::Error, error);
        if (openReleaseOnManualFailure && manualInstallRequired) {
            emit openReleasePageRequested();
        }
        return;
    }

    m_downloadPercent = 0;
    m_downloadError.clear();
    setState(State::Downloading);
    QNetworkRequest request(m_manifest.downloadUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, &ManifestUpdater::writeDownloadedData);
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

void ManifestUpdater::writeDownloadedData()
{
    if (!m_reply || !m_download || !m_downloadError.isEmpty()) {
        return;
    }
    const QByteArray data = m_reply->readAll();
    if (m_download->write(data) != data.size()) {
        m_downloadError = QStringLiteral("Could not write the %1 update file.")
                              .arg(m_downloadDescription);
        m_reply->abort();
    }
}

void ManifestUpdater::finishDownload(QNetworkReply *reply)
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
            ? QStringLiteral("Could not download the %1: %2")
                  .arg(m_downloadDescription, networkError)
            : m_downloadError;
        clearDownload();
        setState(State::Error, error);
        return;
    }

    m_download->flush();
    m_download->close();
    const QString downloadedPath = m_download->fileName();
    m_download.reset();

    QString error;
    if (!verifyDownload(downloadedPath,
                        m_manifest.sha256,
                        m_downloadDescription,
                        &error)
        || !installDownload(downloadedPath, &error)) {
        QFile::remove(downloadedPath);
        setState(State::Error, error);
        return;
    }
    m_downloadPercent = 100;
    setState(State::ReadyToRestart);
}

void ManifestUpdater::clearDownload()
{
    if (!m_download) {
        return;
    }
    const QString path = m_download->fileName();
    m_download->close();
    m_download.reset();
    if (!path.isEmpty()) {
        QFile::remove(path);
    }
}

void ManifestUpdater::setState(State state, const QString &error)
{
    m_state = state;
    m_error = error;
    emit changed();
}

} // namespace speecher
