#pragma once

#include <QObject>
#include <QString>

namespace speecher {

enum class UpdateChannel;

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
        RestartPending,
        Error,
    };
    Q_ENUM(State)

    using QObject::QObject;
    ~UpdateController() override = default;

    virtual void start() = 0;
    virtual State state() const = 0;
    virtual QString currentVersion() const = 0;
    virtual QString availableVersion() const = 0;
    virtual int downloadPercent() const = 0;
    virtual QString errorMessage() const = 0;
    virtual bool isAppImage() const = 0;
    virtual bool bannerVisible() const = 0;

public slots:
    virtual void checkForUpdates(UpdateChannel channel) = 0;
    virtual void updateNow() = 0;
    virtual void dismissAvailableVersion() = 0;

signals:
    void changed();
    void openReleasePageRequested();
};

} // namespace speecher
