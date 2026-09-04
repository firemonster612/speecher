#include "YdotoolSetupTransaction.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>

#include <grp.h>
#include <pwd.h>
#include <unistd.h>

namespace {

using speecher::YdotoolSetupTransaction;

constexpr auto stateDirPath = "/var/lib/speecher";
constexpr auto stateFilePath = "/var/lib/speecher/ydotool-setup.json";
constexpr auto modulesLoadPath = "/etc/modules-load.d/speecher-uinput.conf";
constexpr auto udevRulePath = "/etc/udev/rules.d/70-speecher-uinput.rules";
constexpr auto serviceName = "speecher-ydotoold.service";
constexpr auto groupName = "speecher-uinput";

QString serviceFilePath()
{
    if (QDir(QStringLiteral("/usr/lib/systemd/user")).exists()) {
        return QStringLiteral("/usr/lib/systemd/user/") + QString::fromLatin1(serviceName);
    }
    return QStringLiteral("/lib/systemd/user/") + QString::fromLatin1(serviceName);
}

QString serviceText()
{
    return QStringLiteral(
        "[Unit]\n"
        "Description=Speecher virtual keyboard daemon\n"
        "\n"
        "[Service]\n"
        "Type=simple\n"
        "ExecStart=/usr/bin/ydotoold --socket-path=%t/.ydotool_socket --socket-perm=0600\n"
        "Restart=on-failure\n"
        "RestartSec=1\n"
        "\n"
        "[Install]\n"
        "WantedBy=default.target\n");
}

bool writeFile(const QString &path, const QString &text, QString *error)
{
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!QFileInfo(path).dir().mkpath(QStringLiteral(".")) || !file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = QStringLiteral("Could not write %1").arg(path);
        }
        return false;
    }
    const QByteArray bytes = text.toUtf8();
    if (file.write(bytes) == bytes.size() && file.commit()) {
        return true;
    }
    if (error) {
        *error = QStringLiteral("Could not safely write %1").arg(path);
    }
    return false;
}

bool removeFileIfPresent(const QString &path, QString *error)
{
    if (!QFileInfo::exists(path)) {
        return true;
    }
    if (QFile::remove(path)) {
        return true;
    }
    if (error) {
        *error = QStringLiteral("Could not remove %1").arg(path);
    }
    return false;
}

bool run(const QString &program, const QStringList &args, QString *error, bool ignoreMissing = false, bool ignoreFailure = false)
{
    const QString executable = QStandardPaths::findExecutable(program);
    if (executable.isEmpty()) {
        if (ignoreMissing) {
            return true;
        }
        if (error) {
            *error = QStringLiteral("%1 is not installed").arg(program);
        }
        return false;
    }

    QProcess process;
    process.start(executable, args);
    if (!process.waitForStarted(5000)) {
        if (error) {
            *error = QStringLiteral("Could not start %1").arg(program);
        }
        return false;
    }
    if (process.waitForFinished(300000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
        return true;
    }
    process.kill();
    if (ignoreFailure) {
        return true;
    }
    const QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
    if (error) {
        *error = stderrText.isEmpty() ? QStringLiteral("%1 failed").arg(program) : stderrText;
    }
    return false;
}

bool ydotoolInstalled()
{
    return !QStandardPaths::findExecutable(QStringLiteral("ydotool")).isEmpty()
        && !QStandardPaths::findExecutable(QStringLiteral("ydotoold")).isEmpty();
}

bool installYdotoolPackage(QString *error)
{
    if (ydotoolInstalled()) {
        return true;
    }
    if (!QStandardPaths::findExecutable(QStringLiteral("apt-get")).isEmpty()) {
        return run(QStringLiteral("apt-get"), {QStringLiteral("update")}, error)
            && run(QStringLiteral("apt-get"), {QStringLiteral("install"), QStringLiteral("-y"), QStringLiteral("ydotool")}, error);
    }
    if (!QStandardPaths::findExecutable(QStringLiteral("dnf")).isEmpty()) {
        return run(QStringLiteral("dnf"), {QStringLiteral("install"), QStringLiteral("-y"), QStringLiteral("ydotool")}, error);
    }
    if (!QStandardPaths::findExecutable(QStringLiteral("zypper")).isEmpty()) {
        return run(QStringLiteral("zypper"), {QStringLiteral("--non-interactive"), QStringLiteral("install"), QStringLiteral("ydotool")}, error);
    }
    if (!QStandardPaths::findExecutable(QStringLiteral("pacman")).isEmpty()) {
        return run(QStringLiteral("pacman"), {QStringLiteral("-Sy"), QStringLiteral("--needed"), QStringLiteral("--noconfirm"), QStringLiteral("ydotool")}, error);
    }
    if (error) {
        *error = QStringLiteral("No supported package manager found for installing ydotool");
    }
    return false;
}

bool ensureGroup(bool *created, QString *error)
{
    if (getgrnam(groupName)) {
        return true;
    }
    if (!run(QStringLiteral("groupadd"), {QStringLiteral("--system"), QString::fromLatin1(groupName)}, error)) {
        return false;
    }
    *created = true;
    return true;
}

bool userInGroup(const QString &user)
{
    const QByteArray encoded = user.toLocal8Bit();
    const passwd *account = getpwnam(encoded.constData());
    const group *targetGroup = getgrnam(groupName);
    if (!account || !targetGroup) {
        return false;
    }
    if (account->pw_gid == targetGroup->gr_gid) {
        return true;
    }
    for (char **member = targetGroup->gr_mem; member && *member; ++member) {
        if (encoded == *member) {
            return true;
        }
    }
    return false;
}

bool addUserToGroup(const QString &user, bool *added, QString *error)
{
    if (userInGroup(user)) {
        return true;
    }
    if (!run(QStringLiteral("usermod"), {QStringLiteral("-aG"), QString::fromLatin1(groupName), user}, error)) {
        return false;
    }
    *added = true;
    return true;
}

bool writeState(bool packageWasInstalled, const QString &user, QString *error)
{
    return writeFile(
        QString::fromLatin1(stateFilePath),
        QString::fromUtf8(QJsonDocument(QJsonObject{
                                            {QStringLiteral("packageInstalledBySpeecher"), packageWasInstalled},
                                            {QStringLiteral("targetUser"), user},
                                            {QStringLiteral("serviceFile"), serviceFilePath()},
                                        })
                              .toJson(QJsonDocument::Compact)),
        error);
}

bool validateUser(const QString &user, QString *error)
{
    if (user.isEmpty() || user.contains(QLatin1Char('/')) || user.contains(QLatin1Char(':'))) {
        if (error) {
            *error = QStringLiteral("Invalid target user");
        }
        return false;
    }
    const QByteArray encoded = user.toLocal8Bit();
    if (!getpwnam(encoded.constData())) {
        if (error) {
            *error = QStringLiteral("Target user does not exist");
        }
        return false;
    }
    return true;
}

bool install(const QString &user, QString *error)
{
    YdotoolSetupTransaction transaction;
    const auto failed = [&transaction, error] {
        transaction.appendToError(error);
        return false;
    };
    const bool packageMissingBeforeInstall = !ydotoolInstalled();
    if (!installYdotoolPackage(error)) {
        return failed();
    }
    if (packageMissingBeforeInstall) {
        transaction.record(QStringLiteral("installed the ydotool package"));
    }
    if (!run(QStringLiteral("modprobe"), {QStringLiteral("uinput")}, error)) {
        return failed();
    }
    transaction.record(QStringLiteral("loaded the uinput kernel module"));
    if (!writeFile(QString::fromLatin1(modulesLoadPath), QStringLiteral("uinput\n"), error)) {
        return failed();
    }
    transaction.record(QStringLiteral("wrote %1").arg(QString::fromLatin1(modulesLoadPath)));
    bool groupCreated = false;
    if (!ensureGroup(&groupCreated, error)) {
        return failed();
    }
    if (groupCreated) {
        transaction.record(QStringLiteral("created group %1").arg(QString::fromLatin1(groupName)));
    }
    bool userAdded = false;
    if (!addUserToGroup(user, &userAdded, error)) {
        return failed();
    }
    if (userAdded) {
        transaction.record(QStringLiteral("added %1 to %2").arg(user, QString::fromLatin1(groupName)));
    }
    if (!writeFile(QString::fromLatin1(udevRulePath),
                   QStringLiteral("KERNEL==\"uinput\", SUBSYSTEM==\"misc\", OPTIONS+=\"static_node=uinput\", GROUP=\"speecher-uinput\", MODE=\"0660\", TAG+=\"uaccess\"\n"),
                   error)) {
        return failed();
    }
    transaction.record(QStringLiteral("wrote %1").arg(QString::fromLatin1(udevRulePath)));
    run(QStringLiteral("udevadm"), {QStringLiteral("control"), QStringLiteral("--reload-rules")}, error, true, true);
    run(QStringLiteral("udevadm"), {QStringLiteral("trigger"), QStringLiteral("--subsystem-match=misc"), QStringLiteral("--attr-match=name=uinput")}, error, true, true);
    if (!writeFile(serviceFilePath(), serviceText(), error)) {
        return failed();
    }
    transaction.record(QStringLiteral("wrote %1").arg(serviceFilePath()));
    run(QStringLiteral("systemctl"), {QStringLiteral("--global"), QStringLiteral("enable"), QString::fromLatin1(serviceName)}, error, true, true);
    if (!writeState(packageMissingBeforeInstall, user, error)) {
        return failed();
    }
    return true;
}

bool remove(const QString &user, QString *error)
{
    run(QStringLiteral("systemctl"), {QStringLiteral("--global"), QStringLiteral("disable"), QString::fromLatin1(serviceName)}, error, true, true);
    run(QStringLiteral("gpasswd"), {QStringLiteral("-d"), user, QString::fromLatin1(groupName)}, error, true, true);
    if (!removeFileIfPresent(serviceFilePath(), error)
        || !removeFileIfPresent(QString::fromLatin1(udevRulePath), error)
        || !removeFileIfPresent(QString::fromLatin1(modulesLoadPath), error)
        || !removeFileIfPresent(QString::fromLatin1(stateFilePath), error)) {
        return false;
    }
    run(QStringLiteral("udevadm"), {QStringLiteral("control"), QStringLiteral("--reload-rules")}, error, true, true);
    run(QStringLiteral("udevadm"), {QStringLiteral("trigger"), QStringLiteral("--subsystem-match=misc"), QStringLiteral("--attr-match=name=uinput")}, error, true, true);
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    QString user;
    bool doInstall = false;
    bool doRemove = false;
    for (int i = 1; i < args.size(); ++i) {
        if (args.at(i) == QStringLiteral("--install")) {
            doInstall = true;
        } else if (args.at(i) == QStringLiteral("--remove")) {
            doRemove = true;
        } else if (args.at(i) == QStringLiteral("--user") && i + 1 < args.size()) {
            user = args.at(++i);
        } else {
            QTextStream(stderr) << "Unknown argument\n";
            return 2;
        }
    }
    if (geteuid() != 0) {
        QTextStream(stderr) << "This helper must run as root through pkexec\n";
        return 3;
    }
    QString error;
    if (doInstall == doRemove || !validateUser(user, &error)) {
        QTextStream(stderr) << (error.isEmpty() ? QStringLiteral("Choose exactly one action\n") : error + QLatin1Char('\n'));
        return 2;
    }
    const bool ok = doInstall ? install(user, &error) : remove(user, &error);
    if (!ok) {
        QTextStream(stderr) << error << '\n';
        return 1;
    }
    return 0;
}
