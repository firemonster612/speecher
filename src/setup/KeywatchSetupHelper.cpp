// speecher-keywatch-setup: the pkexec'd installer for speecher-keywatchd. It
// creates the speecher-keywatch system user and group, adds the login user to
// that group (which grants access to the daemon's socket and nothing else),
// installs the socket and service units, and copies the daemon to a system
// path. Only the daemon's own service account gets the input group, through
// the unit's SupplementaryGroups=. The installer deliberately does NOT write
// a udev rule and does NOT touch the login user's input membership: the whole
// point of the design is that no process the user runs gains the ability to
// read input devices. See keywatch-security-design.md.
//
// Modelled on YdotoolSetupHelper.cpp, including its rollback transaction and
// --user validation against the passwd database.

#include "HelperCommands.h"
#include "YdotoolSetupTransaction.h"

#include <iostream>
#include <string>
#include <string_view>

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

constexpr std::string_view socketText =
    "[Unit]\n"
    "Description=Speecher key-watch helper socket\n"
    "\n"
    // World-connectable: the daemon authorizes each peer by its live login
    // session (SO_PEERCRED + sd_uid_get_state), so no login-group membership
    // and no sign-out are needed to reach it. A connecting process with no
    // active session is refused by the daemon before it can watch anything.
    "[Socket]\n"
    "ListenStream=/run/speecher-keywatchd/socket\n"
    "SocketMode=0666\n"
    "SocketUser=root\n"
    "\n"
    "[Install]\n"
    "WantedBy=sockets.target\n";

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

// The daemon and its SELinux module ship beside this installer, so their
// source is our own directory, never a path taken from the caller: nothing
// user-controlled crosses pkexec.
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

// selinuxfs is mounted only when the running kernel has SELinux enabled.
bool selinuxEnabled()
{
    return access("/sys/fs/selinux/enforce", F_OK) == 0;
}

// Under SELinux the copied daemon is plain lib_t, so systemd would run it as
// init_t, which may not open the input devices. The module gives it a
// confined domain of its own; restorecon labels the installed binary so the
// exec from systemd transitions into that domain.
bool loadSelinuxPolicy(std::string &error)
{
    return run("semodule", {"-i", bundledPath(policyFileName)}, error)
        && run("restorecon", {std::string(daemonInstallPath)}, error);
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
    speecher::YdotoolSetupTransaction transaction;
    const auto failed = [&] {
        transaction.appendToError(error);
        return false;
    };
    // user is validated by main() and kept for the CLI contract, but the login
    // user no longer joins any group: the daemon gates the socket by session.
    (void)user;
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
        if (!loadSelinuxPolicy(error)) {
            return failed();
        }
        transaction.record("loaded SELinux module " + std::string(policyModuleName));
    }
    if (!writeFile(std::string(socketUnitPath), socketText, error)) {
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
    (void)user; // No login-user group membership to undo anymore.
    run("systemctl", {"disable", "--now", std::string(socketName)}, error, true, true);
    if (!removeFileIfPresent(std::string(socketUnitPath), error)
        || !removeFileIfPresent(std::string(serviceUnitPath), error)
        || !removeFileIfPresent(std::string(daemonInstallPath), error)) {
        return false;
    }
    if (selinuxEnabled()) {
        run("semodule", {"-r", std::string(policyModuleName)}, error, true, true);
    }
    run("systemctl", {"daemon-reload"}, error, true, true);
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
