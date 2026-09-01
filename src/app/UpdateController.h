#pragma once

#include <QObject>
#include <QUrl>

#include <optional>

class QNetworkAccessManager;
class QNetworkReply;
class QTemporaryFile;
class QTimer;

namespace speecher {

class DictationSession;
class MacSparkleUpdater;
class SettingsStore;

struct UpdateManifest {
    QString version;
    qint64 buildNumber = 0;
    QUrl appImageUrl;
    QByteArray sha256;
};

class UpdateController : public QObject {
    Q_OBJECT

public:
    enum class State {
        Idle,
        Checking,
        UpToDate,
        UpdateAvailable,
        Downloading,
        ReadyToRestart,
        Error,
    };
    Q_ENUM(State)

    UpdateController(SettingsStore *settings,
                     DictationSession *session,
                     QObject *parent = nullptr);
    ~UpdateController() override;

    State state() const;
    QString currentVersion() const;
    qint64 currentBuildNumber() const;
    QString availableVersion() const;
    int downloadPercent() const;
    QString errorMessage() const;
    bool isAppImage() const;
    bool bannerVisible() const;

    static std::optional<UpdateManifest> parseManifest(const QByteArray &json,
                                                       QString *error = nullptr);
    static bool isNewerBuild(const UpdateManifest &manifest, qint64 currentBuildNumber);
    static bool swapAppImage(const QString &downloadedPath,
                             const QString &installedPath,
                             QString *error = nullptr);

public slots:
    void checkForUpdates();
    void updateNow();
    void restartNow();
    void dismissAvailableVersion();

signals:
    void changed();
    void openReleasePageRequested();

private:
    QUrl manifestUrl() const;
    void finishCheck(QNetworkReply *reply);
    void beginDownload();
    void writeDownloadedData();
    void finishDownload(QNetworkReply *reply);
    bool verifyDownload(const QString &path, QString *error) const;
    void clearDownload();
    void setState(State state, const QString &error = {});
    bool dictationSessionActive() const;
    void restartAppImage();

    SettingsStore *m_settings;
    DictationSession *m_session;
    QNetworkAccessManager *m_network;
    QTimer *m_dailyTimer;
    QNetworkReply *m_reply = nullptr;
    QTemporaryFile *m_download = nullptr;
    UpdateManifest m_manifest;
    QString m_appImagePath;
    QString m_downloadError;
    State m_state = State::Idle;
    int m_downloadPercent = 0;
    QString m_error;
    bool m_restartPending = false;
#ifdef Q_OS_MACOS
    MacSparkleUpdater *m_sparkleUpdater = nullptr;
#endif
};

} // namespace speecher
