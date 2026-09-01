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

QString resolvedPath(const QString &path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

QByteArray quotedDesktopExecutable(const QString &path)
{
    QByteArray escaped = QFile::encodeName(path);
    escaped.replace("\\", "\\\\");
    escaped.replace("\"", "\\\"");
    escaped.replace("$", "\\$");
    escaped.replace("`", "\\`");
    return '"' + escaped + '"';
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

QString globalShortcutInstructionCommand(const QString &homePath,
                                         const QString &appImagePath,
                                         const QString &binaryPath)
{
    if (QFileInfo(localBinaryPath(homePath)).isSymLink()) {
        return QStringLiteral("speecher toggle");
    }
    if (!appImagePath.isEmpty()) {
        QString escaped = appImagePath;
        escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
        escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
        escaped.replace(QLatin1Char('$'), QStringLiteral("\\$"));
        escaped.replace(QLatin1Char('`'), QStringLiteral("\\`"));
        return QStringLiteral("\"%1\" toggle").arg(escaped);
    }
    return binaryPath + QStringLiteral(" toggle");
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
    const QByteArray executable = quotedDesktopExecutable(appImagePath);
    for (QByteArray &line : lines) {
        if (!line.startsWith("Exec=")) {
            continue;
        }
        const qsizetype argumentStart = line.indexOf(' ', 5);
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
    if (!QDir().mkpath(binaryDir)
        || !QDir().mkpath(applicationsDir)
        || !QDir().mkpath(iconsDir)) {
        if (error) {
            *error = QStringLiteral("Could not create the user application directories");
        }
        return false;
    }

    const QString targetDesktop = QDir(applicationsDir).filePath(
        QStringLiteral("%1.desktop").arg(QString::fromLatin1(appId)));
    const QString targetIcon = QDir(iconsDir).filePath(
        QStringLiteral("%1.svg").arg(QString::fromLatin1(appId)));
    if (!writeAppImageDesktopFile(sourceDesktop, targetDesktop, image, error)
        || !copyFile(sourceIcon, targetIcon, error)) {
        return false;
    }

    const QString binary = localBinaryPath(homePath);
    const QFileInfo existing(binary);
    if (existing.isSymLink() && resolvedPath(existing.symLinkTarget()) != image) {
        if (!QFile::remove(binary)) {
            if (error) {
                *error = QStringLiteral("Could not replace %1").arg(binary);
            }
            return false;
        }
    } else if (existing.exists() && !existing.isSymLink()) {
        if (error) {
            *error = QStringLiteral("%1 already exists and is not a symbolic link").arg(binary);
        }
        return false;
    }
    if (!QFileInfo(binary).isSymLink() && !QFile::link(image, binary)) {
        if (error) {
            *error = QStringLiteral("Could not create %1").arg(binary);
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
