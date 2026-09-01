#include "platform/LinuxDesktopIntegration.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>

namespace speecher {
namespace {

constexpr auto appId = "io.github.firemonster612.speecher";

QString localBinaryPath(const QString &homePath)
{
    return QDir(homePath).filePath(QStringLiteral(".local/bin/speecher"));
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
        return QStringLiteral("speecher toggle");
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

    if (existing.isSymLink() && resolvedPath(existing.symLinkTarget()) != image) {
        if (!QFile::remove(binary)) {
            if (error) {
                *error = QStringLiteral("Command link installation failed: could not replace %1")
                             .arg(binary);
            }
            return false;
        }
    }
    if (!QFileInfo(binary).isSymLink() && !QFile::link(image, binary)) {
        if (error) {
            *error = QStringLiteral("Command link installation failed: could not create %1")
                         .arg(binary);
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

} // namespace speecher
