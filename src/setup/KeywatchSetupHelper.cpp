// speecher-keywatch-setup: the pkexec'd installer for speecher-keywatchd. It
// creates the speecher-keywatch system account, installs an owner-only socket
// for the login user, installs the service unit, and copies the daemon to a
// system path. Only the daemon's service account gets the input group, through
// the unit's SupplementaryGroups=. The login user can reach the socket but no
// process they run gains access to input devices. See keywatch-security-design.md.
//
// Modelled on YdotoolSetupHelper.cpp, including its rollback transaction and
// --user validation against the passwd database.

#include "HelperCommands.h"
#include "KeywatchPayloadDigests.h"
#include "YdotoolSetupTransaction.h"

#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace speecher::helpers;

namespace {

constexpr std::string_view userName = "speecher-keywatch";
constexpr std::string_view daemonInstallPath = "/usr/local/lib/speecher/speecher-keywatchd";
constexpr std::string_view socketUnitPath = "/etc/systemd/system/speecher-keywatchd.socket";
constexpr std::string_view serviceUnitPath = "/etc/systemd/system/speecher-keywatchd.service";
constexpr std::string_view socketName = "speecher-keywatchd.socket";
constexpr std::string_view serviceName = "speecher-keywatchd.service";
constexpr std::string_view policyFileName = "speecher_keywatchd.cil";
constexpr std::string_view policyModuleName = "speecher_keywatchd";

std::string socketUnitText(const std::string &user)
{
    return
        "[Unit]\n"
        "Description=Speecher key-watch helper socket\n"
        "\n"
        "[Socket]\n"
        "ListenStream=/run/speecher-keywatchd/socket\n"
        "SocketMode=0600\n"
        "SocketUser=" + user + "\n"
        "\n"
        "[Install]\n"
        "WantedBy=sockets.target\n";
}

// The whole sandbox is declared here: the daemon never runs as root. Its
// locked-down service account joins the input group (the login user never
// does), the bounding set is empty, and the syscall allowlist covers what a
// socket-activated epoll loop needs. Editing this file needs root, and so
// would removing any of these lines.
constexpr std::string_view serviceText =
    "[Unit]\n"
    "Description=Speecher key-watch helper\n"
    "Requires=speecher-keywatchd.socket\n"
    "After=speecher-keywatchd.socket\n"
    "\n"
    "[Service]\n"
    "Type=simple\n"
    "ExecStart=/usr/local/lib/speecher/speecher-keywatchd\n"
    "User=speecher-keywatch\n"
    "SupplementaryGroups=input\n"
    "CapabilityBoundingSet=\n"
    "SystemCallFilter=@system-service\n"
    "SystemCallFilter=~@privileged @resources\n"
    "SystemCallErrorNumber=EPERM\n"
    "NoNewPrivileges=yes\n"
    "ProtectSystem=strict\n"
    "ProtectHome=yes\n"
    "PrivateTmp=yes\n"
    "PrivateNetwork=yes\n"
    "IPAddressDeny=any\n"
    "RestrictAddressFamilies=AF_UNIX\n"
    "MemoryDenyWriteExecute=yes\n"
    "SystemCallArchitectures=native\n"
    "LockPersonality=yes\n"
    "RestrictNamespaces=yes\n"
    "ProtectKernelModules=yes\n"
    "ProtectKernelTunables=yes\n"
    "ProtectKernelLogs=yes\n"
    "ProtectClock=yes\n"
    "DeviceAllow=char-input r\n"
    "UMask=0077\n";

// The daemon and its SELinux module ship beside this installer. The app stages
// them as root, then this root-owned binary verifies their build-time digests
// before either payload is installed.
std::string bundledPath(std::string_view name)
{
    char buffer[4096];
    const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0) {
        return {};
    }
    buffer[length] = '\0';
    const std::string self(buffer);
    const std::string::size_type slash = self.find_last_of('/');
    if (slash == std::string::npos) {
        return {};
    }
    return self.substr(0, slash + 1) + std::string(name);
}

// The socket unit names exactly one account in SocketUser= and its mode is
// 0600, so installing for a second account would rewrite that line and lock
// the first account out of a helper it is still using. Read who owns it.
std::string socketUnitOwner()
{
    std::ifstream unit{std::string(socketUnitPath)};
    std::string line;
    const std::string key = "SocketUser=";
    while (std::getline(unit, line)) {
        if (line.rfind(key, 0) != 0) {
            continue;
        }
        std::string owner = line.substr(key.size());
        while (!owner.empty() && (owner.back() == '\r' || owner.back() == ' ')) {
            owner.pop_back();
        }
        return owner;
    }
    return {};
}

// The account the installed unit hands the socket to, when that is somebody
// else. Empty when the helper is this user's, or is not installed at all.
std::string otherAccountOwner(const std::string &user)
{
    const std::string owner = socketUnitOwner();
    return owner == user ? std::string() : owner;
}

// selinuxfs is mounted only when the running kernel has SELinux enabled.
bool selinuxEnabled()
{
    return access("/sys/fs/selinux/enforce", F_OK) == 0;
}

std::string sha256(const std::string &path, std::string &error)
{
    int output[2];
    if (pipe2(output, O_CLOEXEC) != 0) {
        error = "Could not hash " + path;
        return {};
    }
    const pid_t child = fork();
    if (child == 0) {
        close(output[0]);
        if (dup2(output[1], STDOUT_FILENO) < 0) {
            _exit(127);
        }
        close(output[1]);
        execl("/usr/bin/sha256sum", "sha256sum", "--", path.c_str(), nullptr);
        _exit(127);
    }
    close(output[1]);
    if (child < 0) {
        close(output[0]);
        error = "Could not hash " + path;
        return {};
    }
    std::string text;
    char buffer[256];
    ssize_t count = 0;
    while ((count = read(output[0], buffer, sizeof(buffer))) > 0) {
        text.append(buffer, static_cast<std::size_t>(count));
    }
    close(output[0]);
    int status = 0;
    if (count < 0 || waitpid(child, &status, 0) != child
        || !WIFEXITED(status) || WEXITSTATUS(status) != 0
        || text.size() < 65 || text[64] != ' ') {
        error = "Could not hash " + path;
        return {};
    }
    const std::string digest = text.substr(0, 64);
    if (digest.find_first_not_of("0123456789abcdef") != std::string::npos) {
        error = "Could not hash " + path;
        return {};
    }
    return digest;
}

bool verifyPayload(std::string_view name, std::string_view expected, std::string &error)
{
    const std::string path = bundledPath(name);
    if (!path.empty() && sha256(path, error) == expected) {
        return true;
    }
    if (error.empty()) {
        error = "Bundled " + std::string(name) + " failed its integrity check";
    }
    return false;
}

bool removeSelinuxPolicy(std::string &error)
{
    if (!findExecutable("semodule")) {
        return true;
    }
    std::string removeError;
    if (run("semodule", {"-r", std::string(policyModuleName)}, removeError)) {
        return true;
    }
    if (removeError.find(policyModuleName) != std::string::npos
        && removeError.find("No such file or directory") != std::string::npos) {
        return true;
    }
    error = removeError;
    return false;
}

bool ensureSystemUser(bool &created, std::string &error)
{
    if (getpwnam(userName.data())) {
        return true;
    }
    if (!run("useradd",
             {"--system", "--user-group", "--no-create-home",
              "--shell", "/usr/sbin/nologin", std::string(userName)},
             error)) {
        return false;
    }
    created = true;
    return true;
}

bool copyDaemon(std::string &error)
{
    const std::string source = bundledPath("speecher-keywatchd");
    if (source.empty()) {
        error = "Could not locate the bundled speecher-keywatchd";
        return false;
    }
    std::error_code directoryError;
    std::filesystem::create_directories(
        std::filesystem::path(std::string(daemonInstallPath)).parent_path(), directoryError);
    if (directoryError) {
        error = "Could not create the daemon directory";
        return false;
    }
    // Unlink first: opening a running daemon's binary for overwrite fails with
    // ETXTBSY, and repair runs exactly when a wedged daemon may still be
    // alive. A fresh inode replaces the path without touching that process;
    // the socket restart below then brings up the new binary.
    std::error_code removeError;
    std::filesystem::remove(std::string(daemonInstallPath), removeError);
    std::filesystem::copy_file(source, std::string(daemonInstallPath),
                               std::filesystem::copy_options::overwrite_existing, directoryError);
    if (directoryError) {
        error = "Could not install speecher-keywatchd";
        return false;
    }
    std::filesystem::permissions(std::string(daemonInstallPath),
                                 std::filesystem::perms::owner_all
                                     | std::filesystem::perms::group_read
                                     | std::filesystem::perms::group_exec
                                     | std::filesystem::perms::others_read
                                     | std::filesystem::perms::others_exec,
                                 directoryError);
    return true;
}

bool install(const std::string &user, std::string &error)
{
    if (const std::string owner = otherAccountOwner(user); !owner.empty()) {
        error = "The key helper on this computer is already set up for the account \"" + owner
            + "\". Sign in as " + owner + " and remove the key helper there first; setting it up for "
            + user + " now would take it away from " + owner + ".";
        return false;
    }
    speecher::YdotoolSetupTransaction transaction;
    const auto failed = [&] {
        transaction.appendToError(error);
        return false;
    };
    if (!verifyPayload("speecher-keywatchd", speecher::keywatch::daemonSha256, error)
        || !verifyPayload(policyFileName, speecher::keywatch::policySha256, error)) {
        return failed();
    }
    bool userCreated = false;
    if (!ensureSystemUser(userCreated, error)) {
        return failed();
    }
    if (userCreated) {
        transaction.record("created system user " + std::string(userName));
    }
    if (!copyDaemon(error)) {
        return failed();
    }
    transaction.record("installed " + std::string(daemonInstallPath));
    if (selinuxEnabled()) {
        if (!run("semodule", {"-i", bundledPath(policyFileName)}, error)) {
            return failed();
        }
        transaction.record("loaded SELinux module " + std::string(policyModuleName));
        if (!run("restorecon", {std::string(daemonInstallPath)}, error)) {
            return failed();
        }
    }
    if (!writeFile(std::string(socketUnitPath), socketUnitText(user), error)) {
        return failed();
    }
    transaction.record("wrote " + std::string(socketUnitPath));
    if (!writeFile(std::string(serviceUnitPath), serviceText, error)) {
        return failed();
    }
    transaction.record("wrote " + std::string(serviceUnitPath));
    run("systemctl", {"daemon-reload"}, error, true, true);
    // An earlier broken install leaves the service in a crash loop that trips
    // systemd's start-rate limit; clear it, or the first connection after this
    // install is refused for up to ten seconds. Then restart rather than
    // start: systemd labels the socket from the daemon binary when the socket
    // starts, so a socket left listening before the binary carried its domain
    // must be recreated.
    run("systemctl", {"reset-failed", std::string(socketName), std::string(serviceName)},
        error, true, true);
    if (!run("systemctl", {"enable", std::string(socketName)}, error)
        || !run("systemctl", {"restart", std::string(socketName)}, error)) {
        return failed();
    }
    return true;
}

bool remove(const std::string &user, std::string &error)
{
    // Removing somebody else's helper is as much of a theft as replacing it.
    if (const std::string owner = otherAccountOwner(user); !owner.empty()) {
        error = "The key helper on this computer belongs to the account \"" + owner
            + "\". Sign in as " + owner + " and remove it there; removing it for " + user
            + " would take it away from " + owner + ".";
        return false;
    }
    // Every step is attempted and every failure reported, as in
    // YdotoolSetupHelper::remove(): a stubborn file must not leave the daemon
    // binary or the SELinux module behind with only one problem named.
    std::vector<std::string> problems;
    const auto attempt = [&problems](const std::string &what, bool succeeded, const std::string &why) {
        if (!succeeded) {
            problems.push_back(what + (why.empty() ? "" : ": " + why));
        }
    };

    std::string ignored;
    run("systemctl", {"disable", "--now", std::string(socketName)}, ignored, true, true);
    // Stopping the socket does not stop its already-running service: a client
    // holding a connection would keep the daemon reading keyboards after
    // "removed" was reported. Stop the daemon itself too.
    run("systemctl", {"stop", std::string(serviceName)}, ignored, true, true);
    for (const std::string_view path : {socketUnitPath, serviceUnitPath, daemonInstallPath}) {
        std::string stepError;
        attempt("could not remove a file",
                removeFileIfPresent(std::string(path), stepError),
                stepError);
    }
    std::string selinuxError;
    attempt("could not remove the SELinux module",
            removeSelinuxPolicy(selinuxError),
            selinuxError);
    // One-release cleanup for users added to the old socket-access group.
    run("gpasswd", {"-d", user, std::string(userName)}, ignored, true, true);
    run("systemctl", {"daemon-reload"}, ignored, true, true);

    if (problems.empty()) {
        return true;
    }
    error = "Some of the key helper setup could not be removed:";
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
