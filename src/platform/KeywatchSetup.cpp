#include "platform/KeywatchSetup.h"

#include "output/HelperInstall.h"
#include "setup/KeywatchProtocol.h"

#include <QFileInfo>

#include <unistd.h>

namespace speecher {
namespace {

constexpr auto socketUnitPath = "/etc/systemd/system/speecher-keywatchd.socket";
constexpr auto daemonFileName = "speecher-keywatchd";
constexpr auto policyFileName = "speecher_keywatchd.cil";
#ifndef SPEECHER_KEYWATCH_HELPER_PATH
#define SPEECHER_KEYWATCH_HELPER_PATH "/usr/libexec/speecher/speecher-keywatch-setup"
#endif

} // namespace

KeywatchSetupStatus KeywatchSetup::evaluate(const KeywatchProbeFacts &facts)
{
    if (!facts.socketUnitInstalled) {
        return {KeywatchSetupState::NotInstalled,
                QStringLiteral("Not installed"),
                QStringLiteral("The key helper is not installed.")};
    }
    if (!facts.socketExists) {
        return {KeywatchSetupState::DaemonNotRunning,
                QStringLiteral("Installed, not running"),
                QStringLiteral("The key helper's socket is not active.")};
    }
    if (!facts.socketWritable) {
        return {KeywatchSetupState::DaemonNotRunning,
                QStringLiteral("Installed, not reachable"),
                QStringLiteral("The key helper's socket cannot be reached.")};
    }
    return {KeywatchSetupState::Ready,
            QStringLiteral("Ready"),
            QStringLiteral("The key helper is ready.")};
}

KeywatchSetupStatus KeywatchSetup::probe()
{
    KeywatchProbeFacts facts;
    facts.socketUnitInstalled = QFileInfo::exists(QLatin1StringView(socketUnitPath));
    const QFileInfo socket(QLatin1StringView(keywatch::socketPath));
    facts.socketExists = socket.exists();
    // QFileInfo::isWritable() answers from the owner's point of view for a
    // socket node; access() asks the kernel about this process.
    facts.socketWritable = facts.socketExists && access(keywatch::socketPath, W_OK) == 0;
    return evaluate(facts);
}

bool KeywatchSetup::install(QString *error)
{
    return helpers::runSetupHelper(SPEECHER_KEYWATCH_HELPER_PATH,
                                   {QLatin1StringView(daemonFileName),
                                    QLatin1StringView(policyFileName)},
                                   helpers::HelperAction::Install,
                                   error);
}

bool KeywatchSetup::remove(QString *error)
{
    return helpers::runSetupHelper(SPEECHER_KEYWATCH_HELPER_PATH,
                                   {QLatin1StringView(daemonFileName),
                                    QLatin1StringView(policyFileName)},
                                   helpers::HelperAction::Remove,
                                   error);
}

} // namespace speecher
