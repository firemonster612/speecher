#include "HelperCommands.h"
#include "YdotoolSetupState.h"
#include "YdotoolSetupTransaction.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace speecher::helpers;

namespace {

constexpr std::string_view stateFilePath = "/var/lib/speecher/ydotool-setup.json";
constexpr std::string_view modulesLoadPath = "/etc/modules-load.d/speecher-uinput.conf";
constexpr std::string_view udevRulePath = "/etc/udev/rules.d/70-speecher-uinput.rules";
constexpr std::string_view serviceName = "speecher-ydotoold.service";
constexpr std::string_view groupName = "speecher-uinput";

std::string serviceFilePath()
{
    std::error_code error;
    if (std::filesystem::is_directory("/usr/lib/systemd/user", error)) {
        return "/usr/lib/systemd/user/" + std::string(serviceName);
    }
    return "/lib/systemd/user/" + std::string(serviceName);
}

// Installation accepts any ydotoold on PATH, so the unit has to start the one
// that was found. A distribution that puts it outside /usr/bin would otherwise
// pass setup and then fail to start.
std::string serviceText(const std::string &daemonPath)
{
    return
        "[Unit]\n"
        "Description=Speecher virtual keyboard daemon\n"
        "\n"
        "[Service]\n"
        "Type=simple\n"
        "ExecStart=" + daemonPath + " --socket-path=%t/.ydotool_socket --socket-perm=0600\n"
        "Restart=on-failure\n"
        "RestartSec=1\n"
        "\n"
        "[Install]\n"
        "WantedBy=default.target\n";
}

// Everything below installs a systemd user service. Ask before touching the
// machine: on a system systemd does not manage, every package, permission and
// file written here would be wasted and the service would never start.
bool systemdManagesServices(std::string &error)
{
    std::error_code code;
    if (!std::filesystem::is_directory("/run/systemd/system", code)) {
        error = "systemd is not managing this machine's services, so Speecher cannot install "
                "the virtual keyboard service";
        return false;
    }
    if (!findExecutable("systemctl")) {
        error = "systemctl is not installed, so Speecher cannot install the virtual keyboard service";
        return false;
    }
    return true;
}

bool ydotoolInstalled()
{
    return findExecutable("ydotool").has_value() && findExecutable("ydotoold").has_value();
}

bool installYdotoolPackage(std::string &error)
{
    if (ydotoolInstalled()) {
        return true;
    }
    // A slow mirror routinely outlives run()'s default five minutes, and
    // killing a package manager mid-transaction leaves its database damage the
    // transaction machinery deliberately never undoes. Give installs an hour.
    constexpr std::chrono::hours packageDeadline{1};
    if (findExecutable("apt-get")) {
        return run("apt-get", {"update"}, error, false, false, packageDeadline)
            && run("apt-get", {"install", "-y", "ydotool"}, error, false, false, packageDeadline);
    }
    if (findExecutable("dnf")) {
        return run("dnf", {"install", "-y", "ydotool"}, error, false, false, packageDeadline);
    }
    if (findExecutable("zypper")) {
        return run("zypper", {"--non-interactive", "install", "ydotool"}, error,
                   false, false, packageDeadline);
    }
    if (findExecutable("pacman")) {
        error = "Install ydotool through a full Arch system upgrade (sudo pacman -Syu ydotool), then run setup again";
        return false;
    }
    error = "No supported package manager found for installing ydotool";
    return false;
}

bool writeState(bool packageWasInstalled, const std::string &user, std::string &error)
{
    return writeFile(std::string(stateFilePath),
                     speecher::ydotoolSetupStateText(
                         packageWasInstalled, serviceFilePath(), user),
                     error);
}

bool fileExists(const std::string &path)
{
    std::error_code ignored;
    return std::filesystem::exists(path, ignored);
}

// A file this run created can be taken back; one that was already there is a
// file this run replaced, and deleting it would take away whatever configured
// the machine before Speecher did. The stat has to happen before the write, so
// writing and recording belong together.
bool writeSetupFile(const std::string &path,
                    std::string_view text,
                    speecher::YdotoolSetupTransaction &transaction,
                    std::string &error)
{
    const bool existedBefore = fileExists(path);
    if (!writeFile(path, text, error)) {
        return false;
    }
    if (existedBefore) {
        transaction.record("replaced " + path);
        return true;
    }
    transaction.record("wrote " + path, [path] {
        std::string ignored;
        return removeFileIfPresent(path, ignored);
    });
    return true;
}

bool serviceEnabledForEveryAccount()
{
    std::string ignored;
    return run("systemctl", {"--global", "is-enabled", std::string(serviceName)}, ignored);
}

bool install(const std::string &user, std::string &error)
{
    if (!systemdManagesServices(error)) {
        return false;
    }
    speecher::YdotoolSetupTransaction transaction;
    // A repair or a reinstall runs over an installation that already works.
    // Undoing this run's steps there would dismantle that working setup rather
    // than the failed attempt, so a failure reports what it changed and leaves
    // the machine alone.
    const bool setupAlreadyPresent = fileExists(std::string(stateFilePath));
    const auto failed = [&] {
        if (!setupAlreadyPresent) {
            transaction.rollBack();
        }
        transaction.appendToError(error);
        return false;
    };
    const bool packageMissingBeforeInstall = !ydotoolInstalled();
    if (!installYdotoolPackage(error)) {
        return failed();
    }
    if (packageMissingBeforeInstall) {
        transaction.record("installed the ydotool package");
    }
    if (!run("modprobe", {"uinput"}, error)) {
        return failed();
    }
    transaction.record("loaded the uinput kernel module");
    if (!writeSetupFile(std::string(modulesLoadPath), "uinput\n", transaction, error)) {
        return failed();
    }
    bool groupCreated = false;
    if (!ensureGroup(groupName, groupCreated, error)) {
        return failed();
    }
    if (groupCreated) {
        transaction.record("created group " + std::string(groupName), [] {
            std::string ignored;
            return run("groupdel", {std::string(groupName)}, ignored);
        });
    }
    bool userAdded = false;
    if (!addUserToGroup(groupName, user, userAdded, error)) {
        return failed();
    }
    if (userAdded) {
        transaction.record("added " + user + " to " + std::string(groupName), [user] {
            std::string ignored;
            return run("gpasswd", {"-d", user, std::string(groupName)}, ignored);
        });
    }
    if (!writeSetupFile(std::string(udevRulePath),
                        "KERNEL==\"uinput\", SUBSYSTEM==\"misc\", OPTIONS+=\"static_node=uinput\", GROUP=\"speecher-uinput\", MODE=\"0660\", TAG+=\"uaccess\"\n",
                        transaction,
                        error)) {
        return failed();
    }
    run("udevadm", {"control", "--reload-rules"}, error, true, true);
    run("udevadm", {"trigger", "--subsystem-match=misc", "--attr-match=name=uinput"}, error, true, true);
    const std::string servicePath = serviceFilePath();
    if (!writeSetupFile(servicePath,
                        serviceText(findExecutable("ydotoold").value_or("/usr/bin/ydotoold")),
                        transaction,
                        error)) {
        return failed();
    }
    const bool alreadyEnabled = serviceEnabledForEveryAccount();
    if (!run("systemctl", {"--global", "enable", std::string(serviceName)}, error)) {
        return failed();
    }
    if (!alreadyEnabled) {
        transaction.record("enabled " + std::string(serviceName) + " for every account", [] {
            std::string ignored;
            return run("systemctl", {"--global", "disable", std::string(serviceName)}, ignored);
        });
    }
    if (!writeState(packageMissingBeforeInstall, user, error)) {
        return failed();
    }
    return true;
}

// Every step is attempted and every failure is reported. Removing the state
// file while the service still runs or the group membership survives would
// leave the app showing the feature as gone while the access it granted is
// still there, so what did not come off has to reach the person.
bool remove(const std::string &user, std::string &error)
{
    std::vector<std::string> problems;
    const auto attempt = [&problems](const std::string &what, bool succeeded, const std::string &why) {
        if (!succeeded) {
            problems.push_back(what + (why.empty() ? "" : ": " + why));
        }
    };

    std::string stepError;
    // Without systemctl the global enablement link stays, and new sessions
    // keep starting the daemon; that is a leftover, not a step to skip.
    attempt("could not stop starting the service for new sessions",
            run("systemctl", {"--global", "disable", std::string(serviceName)}, stepError),
            stepError);
    if (userInGroup(groupName, user)) {
        stepError.clear();
        attempt("could not remove " + user + " from the " + std::string(groupName) + " group",
                run("gpasswd", {"-d", user, std::string(groupName)}, stepError),
                stepError);
    }
    for (const std::string &path :
         {serviceFilePath(), std::string(udevRulePath), std::string(modulesLoadPath),
          std::string(stateFilePath)}) {
        stepError.clear();
        attempt("could not remove a file", removeFileIfPresent(path, stepError), stepError);
    }
    // The rule file is gone, but udev keeps applying the loaded copy until it
    // reloads, so a silent failure here leaves the group's write access live.
    stepError.clear();
    attempt("could not reload the udev rules",
            run("udevadm", {"control", "--reload-rules"}, stepError),
            stepError);
    stepError.clear();
    attempt("could not re-apply the uinput device permissions",
            run("udevadm",
                {"trigger", "--subsystem-match=misc", "--attr-match=name=uinput"},
                stepError),
            stepError);

    if (problems.empty()) {
        return true;
    }
    error = "Some of the virtual keyboard setup could not be removed:";
    for (const std::string &problem : problems) {
        error += "\n- " + problem;
    }
    return false;
}

void printHelp(const char *program)
{
    std::cout << "Usage: " << program << " (--install|--remove) --user USER\n";
}

} // namespace

int main(int argc, char **argv)
{
    std::string user;
    bool doInstall = false;
    bool doRemove = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--help") {
            printHelp(argv[0]);
            return 0;
        }
        if (argument == "--install") {
            doInstall = true;
        } else if (argument == "--remove") {
            doRemove = true;
        } else if (argument == "--user" && index + 1 < argc) {
            user = argv[++index];
        } else {
            std::cerr << "Unknown argument\n";
            return 2;
        }
    }
    if (geteuid() != 0) {
        std::cerr << "This helper must run as root through pkexec\n";
        return 3;
    }
    std::string error;
    if (doInstall == doRemove || !validateUser(user, error)) {
        std::cerr << (error.empty() ? "Choose exactly one action\n" : error + '\n');
        return 2;
    }
    if (!(doInstall ? install(user, error) : remove(user, error))) {
        std::cerr << error << '\n';
        return 1;
    }
    return 0;
}
