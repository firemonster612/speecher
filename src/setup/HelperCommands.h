#pragma once

// What a privileged setup helper runs: files written atomically, programs run
// without a shell, groups created and joined. Shared by the pkexec'd helpers
// so each keeps only its own installation steps.

#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
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

namespace speecher::helpers {

inline bool writeFile(const std::string &path, std::string_view text, std::string &error, mode_t mode = 0644)
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
    if (fchmod(descriptor, mode) != 0) {
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

inline bool removeFileIfPresent(const std::string &path, std::string &error)
{
    if (unlink(path.c_str()) == 0 || errno == ENOENT) {
        return true;
    }
    error = "Could not remove " + path;
    return false;
}

inline std::optional<std::string> findExecutable(std::string_view program)
{
    // pkexec hands the helpers a sanitized PATH, but sudo often preserves the
    // caller's; a program resolved here runs as root, so root never trusts the
    // environment and searches the fixed system directories only.
    const char *inherited = geteuid() == 0 ? nullptr : std::getenv("PATH");
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

inline std::string readError(FILE *file)
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

inline bool run(std::string_view program,
         const std::vector<std::string> &arguments,
         std::string &error,
         bool ignoreMissing = false,
         bool ignoreFailure = false,
         std::chrono::seconds deadline = std::chrono::minutes(5))
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
    // SIGTERM first, so a child mid-transaction (a package manager, say) can
    // close its books; SIGKILL only for one that ignores the request.
    const auto terminateAt = std::chrono::steady_clock::now() + deadline;
    std::optional<std::chrono::steady_clock::time_point> killAt;
    while ((waited = waitpid(child, &status, WNOHANG)) == 0) {
        const auto now = std::chrono::steady_clock::now();
        if (killAt && now >= *killAt) {
            kill(child, SIGKILL);
            waited = waitpid(child, &status, 0);
            break;
        }
        if (!killAt && now >= terminateAt) {
            kill(child, SIGTERM);
            killAt = now + std::chrono::seconds(10);
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

inline bool ensureGroup(std::string_view groupName, bool &created, std::string &error)
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

inline bool userInGroup(std::string_view groupName, const std::string &user)
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

inline bool addUserToGroup(std::string_view groupName, const std::string &user, bool &added, std::string &error)
{
    if (userInGroup(groupName, user)) {
        return true;
    }
    if (!run("usermod", {"-aG", std::string(groupName), user}, error)) {
        return false;
    }
    added = true;
    return true;
}

inline bool validateUser(const std::string &user, std::string &error)
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

} // namespace speecher::helpers
