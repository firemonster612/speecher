#include "platform/LinuxDesktopIntegration.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace speecher {
namespace {

constexpr auto appId = "io.github.firemonster612.speecher";

QString localBinaryPath(const QString &homePath)
{
    return QDir(homePath).filePath(QStringLiteral(".local/bin/speecher"));
}

QString localDataPath(const QString &homePath)
{
    const QString dataHome = qEnvironmentVariable("XDG_DATA_HOME");
    return dataHome.isEmpty()
        ? QDir(homePath).filePath(QStringLiteral(".local/share"))
        : dataHome;
}

} // namespace

QString resolvedPath(const QString &path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

bool isWaylandSession()
{
    const QString sessionType = qEnvironmentVariable("XDG_SESSION_TYPE").toLower();
    return sessionType == QStringLiteral("wayland")
        || (sessionType.isEmpty() && qEnvironmentVariableIsSet("WAYLAND_DISPLAY"));
}

QStringList desktopShortcutSettingsCommand()
{
    const QStringList desktops = qEnvironmentVariable("XDG_CURRENT_DESKTOP").split(QLatin1Char(':'));
    QStringList command;
    if (desktops.contains(QStringLiteral("KDE"), Qt::CaseInsensitive)) {
        command = {QStringLiteral("systemsettings"), QStringLiteral("kcm_keys")};
    } else if (desktops.contains(QStringLiteral("GNOME"), Qt::CaseInsensitive)) {
        command = {QStringLiteral("gnome-control-center"), QStringLiteral("keyboard")};
    }
    if (command.isEmpty() || QStandardPaths::findExecutable(command.first()).isEmpty()) {
        return {};
    }
    return command;
}

DesktopIntegrationRemoval removeAppImageIntegration(const QString &homePath)
{
    DesktopIntegrationRemoval result;
    const QDir dataHome(localDataPath(homePath));
    const QString applicationsDir = dataHome.filePath(QStringLiteral("applications"));
    const QString desktopFile = QDir(applicationsDir).filePath(
        QStringLiteral("%1.desktop").arg(QString::fromLatin1(appId)));
    const QString icon = dataHome.filePath(
        QStringLiteral("icons/hicolor/scalable/apps/%1.svg")
            .arg(QString::fromLatin1(appId)));
    const QString link = localBinaryPath(homePath);
    const QString helper = dataHome.filePath(
        QStringLiteral("speecher/libexec/speecher-ydotool-setup"));
    const QString runningAppImage = QString::fromLocal8Bit(qgetenv("APPIMAGE"));

    const auto removeItem = [&result, &runningAppImage](const QString &name,
                                                       const QString &path,
                                                       bool onlyLink) {
        const QFileInfo info(path);
        if (!info.exists() && !info.isSymLink()) {
            result.absent.append(name);
            return;
        }
        if (onlyLink && !info.isSymLink()) {
            result.failed.append(
                QStringLiteral("%1: something other than Speecher's link is at %2").arg(name, path));
            return;
        }
        if (onlyLink) {
            const QString target = info.symLinkTarget();
            const QString targetName = QFileInfo(target).fileName();
            const bool isSpeecherAppImage = runningAppImage.isEmpty()
                ? targetName.contains(QStringLiteral("Speecher"), Qt::CaseInsensitive)
                    && targetName.endsWith(QStringLiteral(".AppImage"), Qt::CaseInsensitive)
                : resolvedPath(target) == resolvedPath(runningAppImage);
            if (!isSpeecherAppImage) {
                result.failed.append(
                    QStringLiteral("%1: the link does not point to a Speecher AppImage").arg(name));
                return;
            }
        }
        if (QFile::remove(path)) {
            result.removed.append(name);
        } else {
            result.failed.append(QStringLiteral("%1: could not delete %2").arg(name, path));
        }
    };
    removeItem(QStringLiteral("app menu entry"), desktopFile, false);
    removeItem(QStringLiteral("speecher command"), link, true);
    removeItem(QStringLiteral("app icon"), icon, false);
    removeItem(QStringLiteral("local ydotool setup helper"), helper, false);

    if (!result.removed.isEmpty()) {
        const QString updater = QStandardPaths::findExecutable(
            QStringLiteral("update-desktop-database"));
        if (!updater.isEmpty()) {
            QProcess::startDetached(updater, {applicationsDir});
        }
    }
    return result;
}

} // namespace speecher
