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
    void beginCheck(UpdateChannel channel, bool automaticCheck);
    void updateSettingsChanged();
    QUrl manifestUrl(UpdateChannel channel) const;
    void finishCheck(QNetworkReply *reply);
    void recordAutomaticCheckFailure();
    void beginDownload(bool openReleaseOnManualFailure);
    void writeDownloadedData();
    void finishDownload(QNetworkReply *reply);
    void clearDownload();
    void restartNow();

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
    UpdateChannel m_checkChannel;
    UpdateChannel m_selectedChannel;
    bool m_automaticCheck = false;
    bool m_manualInstallRequired = false;
    int m_automaticCheckFailures = 0;
    State m_state = State::Idle;
    int m_downloadPercent = 0;
    QString m_error;
};

} // namespace speecher
