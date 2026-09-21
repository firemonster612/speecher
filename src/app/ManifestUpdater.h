#pragma once

#include "app/UpdateController.h"
#include "core/AppSettings.h"

#include <QUrl>

#include <memory>
#include <optional>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace speecher {

class DictationSession;
class ManifestUpdaterTestAccess;
class SettingsStore;

struct UpdateManifest {
    QString version;
    qint64 buildNumber = 0;
    QUrl downloadUrl;
    QByteArray sha256;
    UpdateChannel channel = UpdateChannel::Stable;
};

class ManifestUpdater : public UpdateController {
    Q_OBJECT

public:
    ManifestUpdater(SettingsStore *settings,
                    DictationSession *session,
                    QString platformKey,
                    QString downloadKey,
                    QString downloadDescription,
                    QObject *parent = nullptr);
    ~ManifestUpdater() override;

    void start() override;
    State state() const override;
    QString currentVersion() const override;
    qint64 currentBuildNumber() const;
    QString availableVersion() const override;
    QString availableVersionDisplay() const override;
    int downloadPercent() const override;
    QString errorMessage() const override;
    bool bannerVisible() const override;
    bool repeatedAutomaticCheckFailure() const override;
    bool manualInstallRequired() const override;
    bool stableReplacementAvailable() const override;

    static std::optional<UpdateManifest> parseManifest(
        const QByteArray &json,
        const QString &platformKey,
        const QString &downloadKey,
        QString *error = nullptr);
    static bool isNewerBuild(const UpdateManifest &manifest, qint64 currentBuildNumber);
    static bool verifyDownload(const QString &path,
                               const QByteArray &expectedSha,
                               const QString &description,
                               QString *error = nullptr);

public slots:
    void checkForUpdates(UpdateChannel channel) override;
    void updateNow() override;
    void installAndRestart() override;
    void dismissAvailableVersion() override;

protected:
    const UpdateManifest &manifest() const;
    void setState(State state, const QString &error = {});

    virtual std::unique_ptr<QFile> createDownload(QString *error,
                                                   bool *manualInstallRequired) = 0;
    virtual bool installDownload(const QString &path, QString *error) = 0;
    virtual void restartApplication() = 0;

private:
    friend class ManifestUpdaterTestAccess;

    static bool shouldOfferManifest(const UpdateManifest &manifest,
                                    qint64 currentBuildNumber,
                                    const QString &currentVersion,
                                    UpdateChannel channel,
                                    bool automaticCheck);
    // Which of a check's fetched manifests to offer: the newest build wins,
    // and a tie goes to the Stable Release, whose artifact the nightly built
    // from the same commit duplicates.
    static std::optional<UpdateManifest> bestCandidate(
        std::optional<UpdateManifest> primary,
        std::optional<UpdateManifest> stable);
    void beginCheck(UpdateChannel channel, bool automaticCheck);
    void decideCheck(const std::optional<UpdateManifest> &candidate);
    void updateSettingsChanged();
    int baseCheckIntervalMs() const;
    QUrl manifestUrl(UpdateChannel channel) const;
    void finishCheck(QNetworkReply *reply);
    void recordAutomaticCheckFailure();
    void beginDownload(bool openReleaseOnManualFailure);
    void writeDownloadedData();
    void finishDownload(QNetworkReply *reply);
    void clearDownload();
    void restartNow();
    void writeRestoreState();

    SettingsStore *m_settings;
    DictationSession *m_session;
    QNetworkAccessManager *m_network;
    QTimer *m_dailyTimer;
    QNetworkReply *m_reply = nullptr;
    std::unique_ptr<QFile> m_download;
    UpdateManifest m_manifest;
    QString m_platformKey;
    QString m_downloadKey;
    QString m_downloadDescription;
    QString m_downloadError;
    QString m_dismissedVersion;
    QString m_pendingRestoreState;
    // The Nightly channel's check reads both manifests, so a stable release
    // that is the newest build reaches nightly installs too. The nightly
    // manifest lands first and waits here while the stable one is fetched; a
    // failed nightly leg leaves its error here so the stable leg can still
    // offer, and only a check with nothing to offer reports the failure.
    std::optional<UpdateManifest> m_primaryCandidate;
    QString m_primaryCheckError;
    bool m_fetchingStable = false;
    UpdateChannel m_checkChannel;
    UpdateChannel m_selectedChannel;
    bool m_automaticCheck = false;
    bool m_restartWhenReady = false;
    bool m_manualInstallRequired = false;
    int m_automaticCheckFailures = 0;
    State m_state = State::Idle;
    int m_downloadPercent = 0;
    QString m_error;
};

} // namespace speecher
