#pragma once

#include "app/ManifestUpdater.h"

namespace speecher {

class SettingsStore;

class WindowsInstallerUpdater final : public ManifestUpdater {
    Q_OBJECT

public:
    WindowsInstallerUpdater(SettingsStore *settings,
                            QObject *parent = nullptr);

    bool isAppImage() const override;
    bool supportsAutomaticDownloads() const override;

protected:
    std::unique_ptr<QFile> createDownload(QString *error,
                                          bool *manualInstallRequired) override;
    bool installDownload(const QString &path, QString *error) override;
    void restartApplication() override;

private:
    QString m_installerPath;
};

} // namespace speecher
