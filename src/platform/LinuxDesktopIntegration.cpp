#include "platform/LinuxDesktopIntegration.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

#include <cerrno>
#include <cstdio>
#include <cstring>

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

bool readableSource(const QString &path, const QString &artifact, QString *error)
{
    QFile source(path);
    if (source.open(QIODevice::ReadOnly)) {
        return true;
    }
    if (error) {
        *error = QStringLiteral("%1 source is not readable: %2: %3")
                     .arg(artifact, path, source.errorString());
    }
    return false;
}

bool writableDirectoryTarget(const QString &path,
                             const QString &artifact,
                             QString *error)
{
    QString existingPath = path;
    QFileInfo existing(existingPath);
    while (!existing.exists()) {
        const QString parent = existing.dir().absolutePath();
        if (parent == existingPath) {
            break;
        }
        existingPath = parent;
        existing.setFile(existingPath);
    }
    if (existing.isDir() && existing.isWritable()) {
        return true;
    }
    if (error) {
        *error = QStringLiteral("%1 directory is not writable: %2")
                     .arg(artifact, path);
    }
    return false;
}

bool writeFile(const QString &targetPath, const QByteArray &contents, QString *error)
{
    QSaveFile target(targetPath);
    if (!target.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = QStringLiteral("Could not write %1: %2")
                         .arg(targetPath, target.errorString());
        }
        return false;
    }
    if (target.write(contents) != contents.size() || !target.commit()) {
        if (error) {
            *error = QStringLiteral("Could not finish writing %1: %2")
                         .arg(targetPath, target.errorString());
        }
        return false;
    }
    return true;
}

bool copyFile(const QString &sourcePath, const QString &targetPath, QString *error)
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("Could not read %1: %2")
                         .arg(sourcePath, source.errorString());
        }
        return false;
    }
    return writeFile(targetPath, source.readAll(), error);
}

bool replaceCommandLink(const QString &image, const QString &binary, QString *error)
{
    const QString staged = binary + QStringLiteral(".new-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!QFile::link(image, staged)) {
        if (error) {
            *error = QStringLiteral("could not create %1").arg(staged);
        }
        return false;
    }

    const QByteArray stagedName = QFile::encodeName(staged);
    const QByteArray binaryName = QFile::encodeName(binary);
    if (std::rename(stagedName.constData(), binaryName.constData()) == 0) {
        return true;
    }

    const int renameError = errno;
    QFile::remove(staged);
    if (error) {
        *error = QStringLiteral("could not replace %1: %2")
                     .arg(binary, QString::fromLocal8Bit(std::strerror(renameError)));
    }
    return false;
}

} // namespace

QString resolvedPath(const QString &path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

QString quotedExecutablePath(const QString &path)
{
    QString escaped = path;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    escaped.replace(QLatin1Char('$'), QStringLiteral("\\$"));
    escaped.replace(QLatin1Char('`'), QStringLiteral("\\`"));
    return QStringLiteral("\"%1\"").arg(escaped);
}

QString globalShortcutInstructionCommand(const QString &homePath,
                                         const QString &appImagePath,
                                         const QString &binaryPath)
{
    if (!appImagePath.isEmpty()
        && appImageIntegrationInstalled(homePath, appImagePath)) {
        return quotedExecutablePath(localBinaryPath(homePath)) + QStringLiteral(" toggle");
    }
    if (!appImagePath.isEmpty()) {
        return quotedExecutablePath(appImagePath) + QStringLiteral(" toggle");
    }
    return quotedExecutablePath(resolvedPath(binaryPath)) + QStringLiteral(" toggle");
}

bool writeAppImageDesktopFile(const QString &sourcePath,
                              const QString &targetPath,
                              const QString &appImagePath,
                              QString *error)
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("Could not read %1: %2")
                         .arg(sourcePath, source.errorString());
        }
        return false;
    }

    QList<QByteArray> lines = source.readAll().split('\n');
    const QByteArray executable = QFile::encodeName(quotedExecutablePath(appImagePath));
    for (QByteArray &line : lines) {
        if (!line.startsWith("Exec=")) {
            continue;
        }
        qsizetype argumentStart = line.indexOf(' ', 5);
        if (line.startsWith("Exec=\"") && argumentStart >= 0) {
            const qsizetype closingQuote = line.indexOf("\" ", 6);
            argumentStart = closingQuote < 0 ? -1 : closingQuote + 1;
        }
        const QByteArray arguments = argumentStart < 0 ? QByteArray() : line.mid(argumentStart);
        line = QByteArrayLiteral("Exec=") + executable + arguments;
    }
    return writeFile(targetPath, lines.join('\n'), error);
}

bool appImageIntegrationInstalled(const QString &homePath,
                                  const QString &appImagePath)
{
    const QFileInfo link(localBinaryPath(homePath));
    return link.isSymLink()
        && resolvedPath(link.symLinkTarget()) == resolvedPath(appImagePath);
}

bool installAppImageIntegration(const QString &homePath,
                                const QString &appImagePath,
                                const QString &applicationDirPath,
                                QString *error)
{
    const QString image = resolvedPath(appImagePath);
    const QDir appDir(applicationDirPath);
    const QString sourceDesktop = appDir.absoluteFilePath(
        QStringLiteral("../../%1.desktop").arg(QString::fromLatin1(appId)));
    const QString sourceIcon = appDir.absoluteFilePath(
        QStringLiteral("../../%1.svg").arg(QString::fromLatin1(appId)));

    const QDir home(homePath);
    const QString binaryDir = home.filePath(QStringLiteral(".local/bin"));
    const QString applicationsDir = home.filePath(
        QStringLiteral(".local/share/applications"));
    const QString iconsDir = home.filePath(
        QStringLiteral(".local/share/icons/hicolor/scalable/apps"));
    const QString binary = localBinaryPath(homePath);
    const QFileInfo existing(binary);
    if (existing.exists() && !existing.isSymLink()) {
        if (error) {
            *error = QStringLiteral("Command link target already exists and is not a symbolic link: %1")
                         .arg(binary);
        }
        return false;
    }

    const QString targetDesktop = QDir(applicationsDir).filePath(
        QStringLiteral("%1.desktop").arg(QString::fromLatin1(appId)));
    const QString targetIcon = QDir(iconsDir).filePath(
        QStringLiteral("%1.svg").arg(QString::fromLatin1(appId)));
    if (!readableSource(sourceDesktop, QStringLiteral("Desktop file"), error)
        || !readableSource(sourceIcon, QStringLiteral("Icon"), error)
        || !readableSource(image, QStringLiteral("AppImage"), error)
        || !writableDirectoryTarget(binaryDir, QStringLiteral("Command link"), error)
        || !writableDirectoryTarget(applicationsDir, QStringLiteral("Application menu"), error)
        || !writableDirectoryTarget(iconsDir, QStringLiteral("Icon"), error)) {
        return false;
    }

    if (!QDir().mkpath(binaryDir)) {
        if (error) {
            *error = QStringLiteral("Could not create the command link directory: %1")
                         .arg(binaryDir);
        }
        return false;
    }
    if (!QDir().mkpath(applicationsDir)) {
        if (error) {
            *error = QStringLiteral("Could not create the application menu directory: %1")
                         .arg(applicationsDir);
        }
        return false;
    }
    if (!QDir().mkpath(iconsDir)) {
        if (error) {
            *error = QStringLiteral("Could not create the icon directory: %1")
                         .arg(iconsDir);
        }
        return false;
    }

    QString artifactError;
    if (!writeAppImageDesktopFile(sourceDesktop, targetDesktop, image, &artifactError)) {
        if (error) {
            *error = QStringLiteral("Desktop file installation failed: %1").arg(artifactError);
        }
        return false;
    }
    if (!copyFile(sourceIcon, targetIcon, &artifactError)) {
        if (error) {
            *error = QStringLiteral("Icon installation failed: %1").arg(artifactError);
        }
        return false;
    }

    if ((!existing.isSymLink() || resolvedPath(existing.symLinkTarget()) != image)
        && !replaceCommandLink(image, binary, &artifactError)) {
        if (error) {
            *error = QStringLiteral("Command link installation failed: %1").arg(artifactError);
        }
        return false;
    }

    const QString updater = QStandardPaths::findExecutable(
        QStringLiteral("update-desktop-database"));
    if (!updater.isEmpty()) {
        QProcess::startDetached(updater, {applicationsDir});
    }
    return true;
}

DesktopIntegrationRemoval removeAppImageIntegration(const QString &homePath)
{
    DesktopIntegrationRemoval result;
    const QDir home(homePath);
    const QString applicationsDir = home.filePath(QStringLiteral(".local/share/applications"));
    const QString desktopFile = QDir(applicationsDir).filePath(
        QStringLiteral("%1.desktop").arg(QString::fromLatin1(appId)));
    const QString icon = home.filePath(
        QStringLiteral(".local/share/icons/hicolor/scalable/apps/%1.svg")
            .arg(QString::fromLatin1(appId)));
    const QString link = localBinaryPath(homePath);
    const QString helper = QDir(localDataPath(homePath)).filePath(
        QStringLiteral("speecher/libexec/speecher-ydotool-setup"));

    const auto removeItem = [&result](const QString &name, const QString &path, bool onlyLink) {
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
            const QString targetName = QFileInfo(info.symLinkTarget()).fileName();
            if (!targetName.contains(QStringLiteral("Speecher"), Qt::CaseInsensitive)
                || !targetName.endsWith(QStringLiteral(".AppImage"), Qt::CaseInsensitive)) {
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
