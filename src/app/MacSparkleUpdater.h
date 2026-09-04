#pragma once

#include "app/UpdateController.h"

#include <memory>

namespace speecher {

class SettingsStore;

class MacSparkleUpdater final : public UpdateController {
    Q_OBJECT

public:
    explicit MacSparkleUpdater(SettingsStore *settings, QObject *parent = nullptr);
    ~MacSparkleUpdater() override;

    void start() override;
    State state() const override;
    QString currentVersion() const override;
    QString availableVersion() const override;
    int downloadPercent() const override;
    QString errorMessage() const override;
    bool isAppImage() const override;
    bool supportsAutomaticDownloads() const override;
    bool bannerVisible() const override;
    bool repeatedAutomaticCheckFailure() const override;

public slots:
    void checkForUpdates(UpdateChannel channel) override;
    void updateNow() override;
    void dismissAvailableVersion() override;

private:
    struct Native;
    void applySettings();

    SettingsStore *m_settings;
    std::unique_ptr<Native> m_native;
};

} // namespace speecher
