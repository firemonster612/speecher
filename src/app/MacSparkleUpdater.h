#pragma once

#include "app/UpdateController.h"

#include <functional>
#include <memory>

class QTimer;

namespace speecher {

class DictationSession;
class SettingsStore;

class MacSparkleUpdater final : public UpdateController {
    Q_OBJECT

public:
    MacSparkleUpdater(SettingsStore *settings,
                      DictationSession *session,
                      QObject *parent = nullptr);
    ~MacSparkleUpdater() override;

    void start() override;
    State state() const override;
    QString currentVersion() const override;
    QString availableVersion() const override;
    QString availableVersionDisplay() const override;
    int downloadPercent() const override;
    QString errorMessage() const override;
    bool isAppImage() const override;
    bool supportsAutomaticDownloads() const override;
    bool bannerVisible() const override;
    bool repeatedAutomaticCheckFailure() const override;
    bool manualInstallRequired() const override;
    bool stableReplacementAvailable() const override;

    // What our banners answer to Sparkle's "update found" and "ready to
    // install and relaunch" questions.
    enum class Reply { Install, Dismiss };
    using ReplyHandler = std::function<void(Reply)>;

    // The seam the Sparkle user driver drives, public so off-device tests can
    // walk the state machine without an appcast. Sparkle calls its user driver
    // on the main thread, which is also Qt's. Only a user-initiated check makes
    // Sparkle call the check callbacks, so Checking/UpToDate/CheckFailed always
    // mean a manual check the settings banner should report.
    void driverCheckStarted(std::function<void()> cancel = {});
    void driverUpdateFound(const QString &version, qint64 buildNumber, ReplyHandler reply);
    void driverUpToDate();
    void driverDownloadStarted(std::function<void()> cancel = {});
    void driverDownloadExpects(qint64 totalBytes);
    void driverDownloadReceived(qint64 bytes);
    void driverReadyToRestart(ReplyHandler install);
    void driverInstalling();
    void driverFailed(const QString &message);
    void driverSessionEnded();
    bool driverWantsAutomaticChecks() const;

public slots:
    void checkForUpdates(UpdateChannel channel) override;
    void updateNow() override;
    void installAndRestart() override;
    void dismissAvailableVersion() override;

private:
    struct Native;
    void applySettings();
    void updateSettingsChanged();
    void beginBackgroundCheck();
    void cancelActiveSession();
    bool sessionActive() const;
    void setState(State state, const QString &error = {});
    void restartNow();
    void finishRestart();

    SettingsStore *m_settings;
    DictationSession *m_session;
    std::unique_ptr<Native> m_native;
    // Sparkle's schedule clamps to a one-hour minimum, so the sub-hour check
    // frequency is ours to drive, like ManifestUpdater's own timer.
    QTimer *m_checkTimer = nullptr;
    // Clears a transient "up to date" banner a few seconds after a manual check.
    QTimer *m_transientTimer = nullptr;
    State m_state = State::Idle;
    QString m_availableVersion;
    qint64 m_availableBuildNumber = -1;
    QString m_error;
    QString m_dismissedVersion;
    // Captured at the moment of the restart request: a restart deferred to the
    // end of a dictation still restores what the user was doing when they asked.
    QString m_pendingRestoreState;
    ReplyHandler m_updateReply;
    ReplyHandler m_installReply;
    // Sparkle's own cancellation hooks for the two stages that have no reply of
    // their own, so a channel switch can abort an in-flight check or download.
    std::function<void()> m_cancelCheck;
    std::function<void()> m_cancelDownload;
    qint64 m_downloadTotal = 0;
    qint64 m_downloadReceived = 0;
    int m_downloadPercent = 0;
    bool m_restartWhenReady = false;
    bool m_nightlyChannel = false;
    bool m_selectedNightly = false;
};

} // namespace speecher
