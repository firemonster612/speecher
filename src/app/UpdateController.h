#pragma once

#include <QObject>
#include <QString>

#include <functional>

namespace speecher {

enum class UpdateChannel;

// The banner's words for a nightly version string such as
// "0.2.1-nightly.20260921+gabc1234": the build number and commit are what
// actually change between nightlies. Without a build number the date stands in.
inline QString nightlyVersionDisplay(const QString &version, qint64 buildNumber = -1)
{
    const QString commit = version.section(QLatin1Char('+'), 1, 1);
    QString display;
    if (buildNumber >= 0) {
        display = QStringLiteral("nightly build %1").arg(buildNumber);
    } else {
        const QString date = version.section(QStringLiteral("-nightly."), 1, 1)
                                 .section(QLatin1Char('+'), 0, 0);
        display = date.isEmpty() ? QStringLiteral("nightly")
                                 : QStringLiteral("nightly %1").arg(date);
    }
    if (!commit.isEmpty()) {
        display += QStringLiteral(" (%1)").arg(commit);
    }
    return display;
}

class UpdateController : public QObject {
    Q_OBJECT

public:
    enum class State {
        Idle,
        Checking,
        CheckFailed,
        UpToDate,
        UpdateAvailable,
        Downloading,
        ReadyToRestart,
        RestartPending,
        Restarting,
        Error,
    };
    Q_ENUM(State)

    using QObject::QObject;
    ~UpdateController() override = default;

    virtual void start() = 0;
    virtual State state() const = 0;
    virtual QString currentVersion() const = 0;
    virtual QString availableVersion() const = 0;
    // What an update banner calls the offered version. A stable release is its
    // number; a nightly names its build and commit, because its bare version
    // number repeats across builds and reads as "the same update again".
    virtual QString availableVersionDisplay() const { return availableVersion(); }
    virtual int downloadPercent() const = 0;
    virtual QString errorMessage() const = 0;
    virtual bool isAppImage() const = 0;
    virtual bool supportsAutomaticDownloads() const = 0;
    virtual bool bannerVisible() const = 0;
    virtual bool repeatedAutomaticCheckFailure() const = 0;
    virtual bool manualInstallRequired() const = 0;
    virtual bool stableReplacementAvailable() const = 0;

    // Asked what the app should restore after an update restart ("listening",
    // "settings", both, or empty), captured right before the process goes away.
    void setRestoreStateProvider(std::function<QString()> provider)
    {
        m_restoreStateProvider = std::move(provider);
    }

public slots:
    virtual void checkForUpdates(UpdateChannel channel) = 0;
    virtual void updateNow() = 0;
    // One click through the whole tail of the flow: download if needed,
    // install, then restart without waiting for another prompt.
    virtual void installAndRestart() = 0;
    virtual void dismissAvailableVersion() = 0;

signals:
    void changed();
    void openReleasePageRequested();

protected:
    QString restoreState() const
    {
        return m_restoreStateProvider ? m_restoreStateProvider() : QString();
    }

private:
    std::function<QString()> m_restoreStateProvider;
};

} // namespace speecher
