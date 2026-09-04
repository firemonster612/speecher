#include "output/YdotoolSetup.h"

#include "output/HelperPath.h"
#include "output/YdotoolDelivery.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>

#ifdef Q_OS_UNIX
#include <grp.h>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace speecher {

namespace {

constexpr auto groupName = "speecher-uinput";
constexpr auto setupStatePath = "/var/lib/speecher/ydotool-setup.json";
#ifndef SPEECHER_YDOTOOL_HELPER_PATH
#define SPEECHER_YDOTOOL_HELPER_PATH "/usr/libexec/speecher/speecher-ydotool-setup"
#endif

QString installedServicePath()
{
    if (QFileInfo::exists(QStringLiteral("/usr/lib/systemd/user/speecher-ydotoold.service"))) {
        return QStringLiteral("/usr/lib/systemd/user/speecher-ydotoold.service");
    }
    return QStringLiteral("/lib/systemd/user/speecher-ydotoold.service");
}

bool groupExists()
{
#ifdef Q_OS_UNIX
    return getgrnam(groupName) != nullptr;
#else
    return false;
#endif
}

bool userInGroup(const QString &userName)
{
#ifdef Q_OS_UNIX
    const QByteArray user = userName.toLocal8Bit();
    const group *grp = getgrnam(groupName);
    if (!grp) {
        return false;
    }
    for (char **member = grp->gr_mem; member && *member; ++member) {
        if (user == *member) {
            return true;
        }
    }
    const passwd *pw = getpwnam(user.constData());
    return pw && pw->pw_gid == grp->gr_gid;
#else
    Q_UNUSED(userName);
    return false;
#endif
}

bool currentSessionInGroup()
{
#ifdef Q_OS_UNIX
    const group *grp = getgrnam(groupName);
    if (!grp) {
        return false;
    }
    const int count = getgroups(0, nullptr);
    if (count <= 0) {
        return false;
    }
    QList<gid_t> groups(count);
    if (getgroups(count, groups.data()) < 0) {
        return false;
    }
    return groups.contains(grp->gr_gid) || getegid() == grp->gr_gid;
#else
    return false;
#endif
}

QString currentUserName()
{
#ifdef Q_OS_UNIX
    if (const passwd *pw = getpwuid(getuid())) {
        return QString::fromLocal8Bit(pw->pw_name);
    }
#endif
    return qEnvironmentVariable("USER");
}

bool runProgram(const QString &program,
                const QStringList &arguments,
                QString *error,
                int timeoutMs = 60000)
{
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForStarted(3000)) {
        if (error) {
            *error = QStringLiteral("Could not start %1").arg(program);
        }
        return false;
    }
    if (!process.waitForFinished(timeoutMs)
        || process.exitStatus() != QProcess::NormalExit
        || process.exitCode() != 0) {
        process.kill();
        const QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (error) {
            *error = stderrText.isEmpty() ? QStringLiteral("%1 failed").arg(program) : stderrText;
        }
        return false;
    }
    return true;
}

QString stableHelperPath()
{
    const QString dataHome = qEnvironmentVariable("XDG_DATA_HOME");
    if (!dataHome.isEmpty()) {
        return QDir(dataHome).filePath(
            QStringLiteral("speecher/libexec/speecher-ydotool-setup"));
    }
    return QDir(QDir::homePath()).filePath(
        QStringLiteral(".local/libexec/speecher/speecher-ydotool-setup"));
}

bool copyHelper(const QString &sourcePath, const QString &destinationPath, QString *error)
{
    QFile source(sourcePath);
    if (!QFileInfo(source).isFile() || !QFileInfo(source).isExecutable()
        || !source.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("The bundled ydotool setup helper is missing or not executable");
        }
        return false;
    }
    const QByteArray contents = source.readAll();
    if (source.error() != QFileDevice::NoError) {
        if (error) {
            *error = QStringLiteral("Could not read the bundled ydotool setup helper");
        }
        return false;
    }

    const QFileInfo destination(destinationPath);
    if (!destination.dir().mkpath(QStringLiteral("."))) {
        if (error) {
            *error = QStringLiteral("Could not create the local ydotool helper directory");
        }
        return false;
    }
    QSaveFile copy(destinationPath);
    if (!copy.open(QIODevice::WriteOnly)
        || copy.write(contents) != contents.size()
        || !copy.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                | QFileDevice::ExeOwner | QFileDevice::ReadGroup
                                | QFileDevice::ExeGroup | QFileDevice::ReadOther
                                | QFileDevice::ExeOther)
        || !copy.commit()) {
        if (error) {
            *error = QStringLiteral("Could not install the local ydotool setup helper");
        }
        return false;
    }

    QFile verified(destinationPath);
    if (!verified.open(QIODevice::ReadOnly) || verified.readAll() != contents
        || verified.error() != QFileDevice::NoError) {
        QFile::remove(destinationPath);
        if (error) {
            *error = QStringLiteral("The local ydotool setup helper could not be verified");
        }
        return false;
    }
    return true;
}

} // namespace

bool YdotoolSetupStatus::ready() const
{
    return state == YdotoolSetupState::Ready || state == YdotoolSetupState::ReadyLayoutCaveat;
}

bool YdotoolSetupStatus::canEnable() const
{
    return ready() || state == YdotoolSetupState::Disabled;
}

YdotoolSetupStatus YdotoolSetup::evaluate(const YdotoolProbeFacts &facts)
{
    if (!facts.enabledInSpeecher
        && facts.ydotoolInstalled
        && facts.ydotooldInstalled
        && facts.uinputExists
        && facts.uinputReadWrite
        && facts.socketExists
        && facts.socketWritable) {
        return {YdotoolSetupState::Disabled,
                facts.speecherManagedSetupInstalled ? QStringLiteral("Disabled in Speecher") : QStringLiteral("Available outside Speecher"),
                facts.speecherManagedSetupInstalled
                    ? QStringLiteral("Speecher-managed virtual keyboard setup is installed but not enabled in Speecher.")
                    : QStringLiteral("System ydotool is available, but Speecher-managed setup is not installed."),
                facts.speecherManagedSetupInstalled};
    }

    if (!facts.ydotoolInstalled || !facts.ydotooldInstalled) {
        return {YdotoolSetupState::NotInstalled,
                QStringLiteral("Not installed"),
                QStringLiteral("ydotool or ydotoold is missing."),
                facts.speecherManagedSetupInstalled};
    }
    if (!facts.uinputExists || !facts.uinputReadWrite) {
        if (facts.userInConfiguredGroup && !facts.currentSessionInConfiguredGroup) {
            return {YdotoolSetupState::NeedsSignOut,
                    QStringLiteral("Needs sign out"),
                    QStringLiteral("Sign out and back in so this session picks up virtual keyboard permissions."),
                    facts.speecherManagedSetupInstalled};
        }
        return {YdotoolSetupState::NeedsUinputPermission,
                QStringLiteral("Needs uinput permission"),
                QStringLiteral("The ydotool daemon cannot access /dev/uinput yet."),
                facts.speecherManagedSetupInstalled};
    }
    if (!facts.socketExists || !facts.socketWritable) {
        return {YdotoolSetupState::DaemonNotRunning,
                QStringLiteral("Installed, daemon not running"),
                QStringLiteral("The ydotool daemon socket is not available."),
                facts.speecherManagedSetupInstalled};
    }
    return {YdotoolSetupState::Ready,
            QStringLiteral("Ready"),
            QStringLiteral("Speecher can use virtual keyboard input."),
            facts.speecherManagedSetupInstalled};
}

YdotoolSetupStatus YdotoolSetup::probe(bool enabledInSpeecher)
{
    YdotoolProbeFacts facts;
    facts.enabledInSpeecher = enabledInSpeecher;
    facts.ydotoolInstalled = !QStandardPaths::findExecutable(QStringLiteral("ydotool")).isEmpty();
    facts.ydotooldInstalled = !QStandardPaths::findExecutable(QStringLiteral("ydotoold")).isEmpty();
    const QFileInfo uinput(QStringLiteral("/dev/uinput"));
    facts.uinputExists = uinput.exists();
    facts.uinputReadWrite = uinput.isReadable() && uinput.isWritable();
    facts.speecherGroupExists = groupExists();
    facts.speecherManagedSetupInstalled = QFileInfo::exists(QString::fromLatin1(setupStatePath))
        || QFileInfo::exists(installedServicePath());
    const QString user = currentUserName();
    facts.userInConfiguredGroup = !user.isEmpty() && userInGroup(user);
    facts.currentSessionInConfiguredGroup = currentSessionInGroup();
    const QFileInfo socket(YdotoolDelivery::socketPath());
    facts.socketExists = socket.exists();
    facts.socketWritable = socket.isWritable();
    return evaluate(facts);
}

QString YdotoolSetup::stateId(YdotoolSetupState state)
{
    switch (state) {
    case YdotoolSetupState::Disabled:
        return QStringLiteral("disabled");
    case YdotoolSetupState::NotInstalled:
        return QStringLiteral("not_installed");
    case YdotoolSetupState::DaemonNotRunning:
        return QStringLiteral("daemon_not_running");
    case YdotoolSetupState::NeedsUinputPermission:
        return QStringLiteral("needs_uinput_permission");
    case YdotoolSetupState::NeedsSignOut:
        return QStringLiteral("needs_sign_out");
    case YdotoolSetupState::Ready:
        return QStringLiteral("ready");
    case YdotoolSetupState::ReadyLayoutCaveat:
        return QStringLiteral("ready_layout_caveat");
    }
    return QStringLiteral("unknown");
}

QString YdotoolSetup::serviceName()
{
    return QStringLiteral("speecher-ydotoold.service");
}

QString YdotoolSetup::helperPath(QString *error)
{
    const QString bundled = resolvedHelperPath(SPEECHER_YDOTOOL_HELPER_PATH);
    if (qEnvironmentVariableIsSet("APPIMAGE")) {
        const QString stable = stableHelperPath();
        return copyHelper(bundled, stable, error) ? stable : QString();
    }
    return bundled;
}

bool YdotoolSetup::runHelper(HelperAction action, QString *error)
{
    const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    if (pkexec.isEmpty()) {
        if (error) {
            *error = QStringLiteral("pkexec is not installed");
        }
        return false;
    }
    const QString user = currentUserName();
    if (user.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Could not determine the current user");
        }
        return false;
    }
    const QString helper = helperPath(error);
    if (helper.isEmpty()) {
        return false;
    }
    return runProgram(pkexec,
                      {helper,
                       action == HelperAction::Install ? QStringLiteral("--install") : QStringLiteral("--remove"),
                       QStringLiteral("--user"),
                       user},
                      error,
                      5 * 60 * 1000);
}

bool YdotoolSetup::startUserService(QString *error)
{
    const QString systemctl = QStandardPaths::findExecutable(QStringLiteral("systemctl"));
    if (systemctl.isEmpty()) {
        if (error) {
            *error = QStringLiteral("systemctl is not installed");
        }
        return false;
    }
    QString ignored;
    runProgram(systemctl, {QStringLiteral("--user"), QStringLiteral("daemon-reload")}, &ignored);
    return runProgram(systemctl,
                      {QStringLiteral("--user"), QStringLiteral("enable"), QStringLiteral("--now"), serviceName()},
                      error);
}

bool YdotoolSetup::stopUserService(QString *error)
{
    const QString systemctl = QStandardPaths::findExecutable(QStringLiteral("systemctl"));
    if (systemctl.isEmpty()) {
        if (error) {
            *error = QStringLiteral("systemctl is not installed");
        }
        return false;
    }
    return runProgram(systemctl,
                      {QStringLiteral("--user"), QStringLiteral("disable"), QStringLiteral("--now"), serviceName()},
                      error);
}

} // namespace speecher
