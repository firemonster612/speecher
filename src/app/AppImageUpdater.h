#pragma once

#include "app/UpdateController.h"

#include <QUrl>

#include <optional>

class QNetworkAccessManager;
class QNetworkReply;
class QTemporaryFile;
class QTimer;

namespace speecher {

class DictationSession;
class SettingsStore;
class AppImageUpdaterTestAccess;

struct UpdateManifest {
    QString version;
    qint64 buildNumber = 0;
    QUrl appImageUrl;
    QByteArray sha256;
};

struct AppImageFileIdentity {
    quint64 inode = 0;
    qint64 modifiedSeconds = 0;
    qint64 modifiedNanoseconds = 0;

    bool operator==(const AppImageFileIdentity &) const = default;
};

class AppImageUpdater final : public UpdateController {
    Q_OBJECT

public:
    AppImageUpdater(SettingsStore *settings,
                    DictationSession *session,
                    QObject *parent = nullptr);
    ~AppImageUpdater() override;

    void start() override;
    State state() const override;
    QString currentVersion() const override;
    qint64 currentBuildNumber() const;
    QString availableVersion() const override;
    int downloadPercent() const override;
    QString errorMessage() const override;
    bool isAppImage() const override;
    bool supportsAutomaticDownloads() const override;
    bool bannerVisible() const override;

    static std::optional<UpdateManifest> parseManifest(const QByteArray &json,
                                                       QString *error = nullptr);
    static bool isNewerBuild(const UpdateManifest &manifest, qint64 currentBuildNumber);
    static bool verifyDownload(const QString &path,
                               const QByteArray &expectedSha,
                               QString *error = nullptr);
    static std::optional<AppImageFileIdentity> fileIdentity(
        const QString &path,
        QString *error = nullptr);
    static bool swapAppImage(const QString &downloadedPath,
                             const QString &installedPath,
                             const AppImageFileIdentity &expectedIdentity,
                             QString *error = nullptr);

public slots:
    void checkForUpdates(UpdateChannel channel) override;
    void updateNow() override;
    void dismissAvailableVersion() override;

private:
    friend class AppImageUpdaterTestAccess;

    static bool shouldOfferManifest(const UpdateManifest &manifest,
                                    qint64 currentBuildNumber,
                                    const QString &currentVersion,
                                    UpdateChannel channel,
                                    bool automaticCheck);
    void beginCheck(UpdateChannel channel, bool automaticCheck);
    QUrl manifestUrl(UpdateChannel channel) const;
    void finishCheck(QNetworkReply *reply);
    void beginDownload();
    void writeDownloadedData();
    void finishDownload(QNetworkReply *reply);
    void clearDownload();
    void setState(State state, const QString &error = {});
    void restartNow();
    void restartAppImage();

    SettingsStore *m_settings;
    DictationSession *m_session;
    QNetworkAccessManager *m_network;
    QTimer *m_dailyTimer;
    QNetworkReply *m_reply = nullptr;
    QTemporaryFile *m_download = nullptr;
    UpdateManifest m_manifest;
    std::optional<AppImageFileIdentity> m_appImageIdentity;
    QString m_appImagePath;
    QString m_downloadError;
    QString m_dismissedVersion;
    UpdateChannel m_checkChannel;
    bool m_automaticCheck = false;
    State m_state = State::Idle;
    int m_downloadPercent = 0;
    QString m_error;
};

} // namespace speecher
