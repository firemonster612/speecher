#include "output/YdotoolSetup.h"

#include "output/HelperInstall.h"
#include "output/YdotoolDelivery.h"

#include <QFileInfo>
#include <QStandardPaths>

#include <grp.h>

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

} // namespace

bool YdotoolSetupStatus::ready() const
{
    return state == YdotoolSetupState::Ready || state == YdotoolSetupState::ReadyLayoutCaveat;
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
                QStringLiteral("A required package is missing. Set up installs it."),
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
    facts.speecherGroupExists = getgrnam(groupName) != nullptr;
    facts.speecherManagedSetupInstalled = QFileInfo::exists(QString::fromLatin1(setupStatePath))
        || QFileInfo::exists(installedServicePath());
    const QString user = helpers::currentUserName();
    facts.userInConfiguredGroup = !user.isEmpty() && helpers::userInGroup(groupName, user);
    facts.currentSessionInConfiguredGroup = helpers::currentSessionInGroup(groupName);
    const QFileInfo socket(YdotoolDelivery::socketPath());
    facts.socketExists = socket.exists();
    facts.socketWritable = socket.isWritable();
    return evaluate(facts);
}

QString YdotoolSetup::serviceName()
{
    return QStringLiteral("speecher-ydotoold.service");
}

bool YdotoolSetup::runHelper(HelperAction action, QString *error)
{
    return helpers::runSetupHelper(SPEECHER_YDOTOOL_HELPER_PATH,
                                   {},
                                   action == HelperAction::Install ? helpers::HelperAction::Install
                                                                   : helpers::HelperAction::Remove,
                                   error);
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
    helpers::runProgram(systemctl, {QStringLiteral("--user"), QStringLiteral("daemon-reload")}, &ignored);
    return helpers::runProgram(systemctl,
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
    return helpers::runProgram(systemctl,
                      {QStringLiteral("--user"), QStringLiteral("disable"), QStringLiteral("--now"), serviceName()},
                      error);
}

} // namespace speecher
