#pragma once

#include "app/ManifestUpdater.h"

#include <QProcessEnvironment>

class QLocalServer;

namespace speecher {

class AppImageUpdaterTestAccess;
class DictationSession;
class SettingsStore;

struct AppImageFileIdentity {
    quint64 inode = 0;
    qint64 modifiedSeconds = 0;
    qint64 modifiedNanoseconds = 0;

    bool operator==(const AppImageFileIdentity &) const = default;
};

class AppImageUpdater final : public ManifestUpdater {
    Q_OBJECT

public:
    AppImageUpdater(SettingsStore *settings,
                    DictationSession *session,
                    QObject *parent = nullptr);

    bool isAppImage() const override;
    bool supportsAutomaticDownloads() const override;
    static void waitForRestartParent();

    static std::optional<AppImageFileIdentity> fileIdentity(
        const QString &path,
        QString *error = nullptr);
    static bool swapAppImage(const QString &downloadedPath,
                             const QString &installedPath,
                             const AppImageFileIdentity &expectedIdentity,
                             QString *error = nullptr);

protected:
    std::unique_ptr<QFile> createDownload(QString *error,
                                          bool *manualInstallRequired) override;
    bool installDownload(const QString &path, QString *error) override;
    void restartApplication() override;

private:
    friend class AppImageUpdaterTestAccess;

    static QProcessEnvironment restartEnvironment(const QStringList &arguments,
                                                   QProcessEnvironment environment);

    std::optional<AppImageFileIdentity> m_appImageIdentity;
    QString m_appImagePath;
    QLocalServer *m_restartServer = nullptr;
};

} // namespace speecher
