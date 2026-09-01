#pragma once

#include <QObject>

#include <memory>

namespace speecher {

class SettingsStore;

class MacSparkleUpdater final : public QObject {
public:
    explicit MacSparkleUpdater(SettingsStore *settings, QObject *parent = nullptr);
    ~MacSparkleUpdater() override;

    void checkForUpdates();

private:
    struct Native;
    void applySettings();

    SettingsStore *m_settings;
    std::unique_ptr<Native> m_native;
};

} // namespace speecher
