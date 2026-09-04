#include "YdotoolSetupState.h"
#include "YdotoolSetupTransaction.h"

#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

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

constexpr std::string_view serviceText =
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
    "WantedBy=default.target\n";

bool writeFile(const std::string &path, std::string_view text, std::string &error)
{
    std::error_code directoryError;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), directoryError);
    if (directoryError) {
        error = "Could not write " + path;
        return false;
    }

    std::string temporary = path + ".tmp.XXXXXX";
    std::vector<char> name(temporary.begin(), temporary.end());
    name.push_back('\0');
    const int descriptor = mkstemp(name.data());
    if (descriptor < 0) {
        error = "Could not write " + path;
        return false;
    }
    const auto discard = [&] {
        close(descriptor);
        unlink(name.data());
    };
    if (fchmod(descriptor, 0644) != 0) {
        discard();
        error = "Could not safely write " + path;
        return false;
    }
    std::size_t written = 0;
    while (written < text.size()) {
        const ssize_t result = ::write(descriptor, text.data() + written, text.size() - written);
        if (result <= 0) {
            discard();
            error = "Could not safely write " + path;
            return false;
        }
        written += static_cast<std::size_t>(result);
    }
    const bool synchronized = fsync(descriptor) == 0;
    const bool closed = close(descriptor) == 0;
    if (!synchronized || !closed || rename(name.data(), path.c_str()) != 0) {
        unlink(name.data());
        error = "Could not safely write " + path;
        return false;
    }
    return true;
}

bool removeFileIfPresent(const std::string &path, std::string &error)
{
    if (unlink(path.c_str()) == 0 || errno == ENOENT) {
        return true;
    }
    error = "Could not remove " + path;
    return false;
}

std::optional<std::string> findExecutable(std::string_view program)
{
    const char *inherited = std::getenv("PATH");
    const std::string path = inherited && *inherited
        ? inherited
        : "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";
    std::size_t begin = 0;
    while (begin <= path.size()) {
        const std::size_t end = path.find(':', begin);
        const std::string directory = path.substr(begin, end - begin);
        if (!directory.empty() && directory.front() == '/') {
            const std::string candidate = directory + "/" + std::string(program);
            struct stat status {};
            if (stat(candidate.c_str(), &status) == 0
                && S_ISREG(status.st_mode)
                && access(candidate.c_str(), X_OK) == 0) {
                return candidate;
            }
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    return std::nullopt;
}

std::string readError(FILE *file)
{
    std::string result;
    std::rewind(file);
    char buffer[4096];
    while (const std::size_t count = std::fread(buffer, 1, sizeof(buffer), file)) {
        result.append(buffer, count);
    }
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
        result.pop_back();
    }
    return result;
}

bool run(std::string_view program,
         const std::vector<std::string> &arguments,
         std::string &error,
         bool ignoreMissing = false,
         bool ignoreFailure = false)
{
    const std::optional<std::string> executable = findExecutable(program);
    if (!executable) {
        if (ignoreMissing) {
            return true;
        }
        error = std::string(program) + " is not installed";
        return false;
    }

    FILE *stderrFile = std::tmpfile();
    if (!stderrFile) {
        error = "Could not start " + std::string(program);
        return false;
    }
    const pid_t child = fork();
    if (child == 0) {
        const int stdoutFile = open("/dev/null", O_WRONLY);
        if (stdoutFile < 0
            || (stdoutFile != STDOUT_FILENO && dup2(stdoutFile, STDOUT_FILENO) < 0)) {
            _exit(127);
        }
        if (stdoutFile != STDOUT_FILENO) {
            close(stdoutFile);
        }
        dup2(fileno(stderrFile), STDERR_FILENO);
        std::vector<char *> argv;
        argv.reserve(arguments.size() + 2);
        argv.push_back(const_cast<char *>(executable->c_str()));
        for (const std::string &argument : arguments) {
            argv.push_back(const_cast<char *>(argument.c_str()));
        }
        argv.push_back(nullptr);
        execv(executable->c_str(), argv.data());
        _exit(127);
    }
    if (child < 0) {
        std::fclose(stderrFile);
        error = "Could not start " + std::string(program);
        return false;
    }

    int status = 0;
    pid_t waited = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(5);
    while ((waited = waitpid(child, &status, WNOHANG)) == 0) {
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(child, SIGKILL);
            waited = waitpid(child, &status, 0);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const std::string stderrText = readError(stderrFile);
    std::fclose(stderrFile);
    if (waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        return true;
    }
    if (ignoreFailure) {
        return true;
    }
    error = stderrText.empty() ? std::string(program) + " failed" : stderrText;
    return false;
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
    if (findExecutable("apt-get")) {
        return run("apt-get", {"update"}, error)
            && run("apt-get", {"install", "-y", "ydotool"}, error);
    }
    if (findExecutable("dnf")) {
        return run("dnf", {"install", "-y", "ydotool"}, error);
    }
    if (findExecutable("zypper")) {
        return run("zypper", {"--non-interactive", "install", "ydotool"}, error);
    }
    if (findExecutable("pacman")) {
        error = "Install ydotool through a full Arch system upgrade (sudo pacman -Syu ydotool), then run setup again";
        return false;
    }
    error = "No supported package manager found for installing ydotool";
    return false;
}

bool ensureGroup(bool &created, std::string &error)
{
    if (getgrnam(groupName.data())) {
        return true;
    }
    if (!run("groupadd", {"--system", std::string(groupName)}, error)) {
        return false;
    }
    created = true;
    return true;
}

bool userInGroup(const std::string &user)
{
    const struct passwd *account = getpwnam(user.c_str());
    const struct group *targetGroup = getgrnam(groupName.data());
    if (!account || !targetGroup) {
        return false;
    }
    if (account->pw_gid == targetGroup->gr_gid) {
        return true;
    }
    for (char **member = targetGroup->gr_mem; member && *member; ++member) {
        if (user == *member) {
            return true;
        }
    }
    return false;
}

bool addUserToGroup(const std::string &user, bool &added, std::string &error)
{
    if (userInGroup(user)) {
        return true;
    }
    if (!run("usermod", {"-aG", std::string(groupName), user}, error)) {
        return false;
    }
    added = true;
    return true;
}

bool writeState(bool packageWasInstalled, const std::string &user, std::string &error)
{
    return writeFile(std::string(stateFilePath),
                     speecher::ydotoolSetupStateText(
                         packageWasInstalled, serviceFilePath(), user),
                     error);
}

bool validateUser(const std::string &user, std::string &error)
{
    if (user.empty() || user.find('/') != std::string::npos || user.find(':') != std::string::npos) {
        error = "Invalid target user";
        return false;
    }
    if (!getpwnam(user.c_str())) {
        error = "Target user does not exist";
        return false;
    }
    return true;
}

bool install(const std::string &user, std::string &error)
{
    speecher::YdotoolSetupTransaction transaction;
    const auto failed = [&] {
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
    if (!writeFile(std::string(modulesLoadPath), "uinput\n", error)) {
        return failed();
    }
    transaction.record("wrote " + std::string(modulesLoadPath));
    bool groupCreated = false;
    if (!ensureGroup(groupCreated, error)) {
        return failed();
    }
    if (groupCreated) {
        transaction.record("created group " + std::string(groupName));
    }
    bool userAdded = false;
    if (!addUserToGroup(user, userAdded, error)) {
        return failed();
    }
    if (userAdded) {
        transaction.record("added " + user + " to " + std::string(groupName));
    }
    if (!writeFile(std::string(udevRulePath),
                   "KERNEL==\"uinput\", SUBSYSTEM==\"misc\", OPTIONS+=\"static_node=uinput\", GROUP=\"speecher-uinput\", MODE=\"0660\", TAG+=\"uaccess\"\n",
                   error)) {
        return failed();
    }
    transaction.record("wrote " + std::string(udevRulePath));
    run("udevadm", {"control", "--reload-rules"}, error, true, true);
    run("udevadm", {"trigger", "--subsystem-match=misc", "--attr-match=name=uinput"}, error, true, true);
    const std::string servicePath = serviceFilePath();
    if (!writeFile(servicePath, serviceText, error)) {
        return failed();
    }
    transaction.record("wrote " + servicePath);
    run("systemctl", {"--global", "enable", std::string(serviceName)}, error, true, true);
    if (!writeState(packageMissingBeforeInstall, user, error)) {
        return failed();
    }
    return true;
}

bool remove(const std::string &user, std::string &error)
{
    run("systemctl", {"--global", "disable", std::string(serviceName)}, error, true, true);
    run("gpasswd", {"-d", user, std::string(groupName)}, error, true, true);
    if (!removeFileIfPresent(serviceFilePath(), error)
        || !removeFileIfPresent(std::string(udevRulePath), error)
        || !removeFileIfPresent(std::string(modulesLoadPath), error)
        || !removeFileIfPresent(std::string(stateFilePath), error)) {
        return false;
    }
    run("udevadm", {"control", "--reload-rules"}, error, true, true);
    run("udevadm", {"trigger", "--subsystem-match=misc", "--attr-match=name=uinput"}, error, true, true);
    return true;
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
