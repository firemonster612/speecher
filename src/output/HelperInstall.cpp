#include "output/HelperInstall.h"

#include "output/HelperPath.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cerrno>
#include <csignal>
#include <vector>

#include <fcntl.h>
#include <grp.h>
#include <poll.h>
#include <pty.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

namespace speecher::helpers {
namespace {

// Where a helper that does not already live at a root-owned path is staged,
// by root, before root executes it.
constexpr auto rootStageDirectory = "/usr/local/lib/speecher/setup";

PkexecPrompt promptHandler;

// How long the terminal must stay quiet before text ending in a colon counts
// as a prompt waiting for input rather than output still arriving.
constexpr int promptQuietMs = 150;

void setNonBlocking(int descriptor)
{
    fcntl(descriptor, F_SETFL, fcntl(descriptor, F_GETFL) | O_NONBLOCK);
}

// Returns false once the descriptor reaches end of file. A PTY master reports
// its child gone as EIO, which counts too.
bool drainDescriptor(int descriptor, QByteArray *into)
{
    while (true) {
        char buffer[1024];
        const ssize_t count = read(descriptor, buffer, sizeof buffer);
        if (count > 0) {
            into->append(buffer, count);
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return true;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
}

// The agent decorates its banner with color escapes and ends lines with CR LF.
QString terminalText(const QByteArray &raw)
{
    static const QRegularExpression controlSequences(
        QStringLiteral("\x1B\\[[0-9;]*[A-Za-z]"));
    QString text = QString::fromLocal8Bit(raw);
    text.remove(controlSequences);
    text.remove(QLatin1Char('\r'));
    return text;
}

// A prompt is an unterminated last line ending in a colon: "Password: " or
// "Choose identity to authenticate as (1-2): ".
bool waitingAtPrompt(const QByteArray &conversation)
{
    const qsizetype lineStart = conversation.lastIndexOf('\n') + 1;
    QByteArrayView line = QByteArrayView(conversation).sliced(lineStart);
    while (line.endsWith(' ')) {
        line.chop(1);
    }
    return line.endsWith(':');
}

void writeReply(int terminal, const QString &reply)
{
    const QByteArray line = reply.toLocal8Bit() + '\n';
    for (qsizetype written = 0; written < line.size();) {
        const ssize_t count =
            write(terminal, line.constData() + written, line.size() - written);
        if (count < 0 && (errno == EINTR || errno == EAGAIN)) {
            pollfd writable{terminal, POLLOUT, 0};
            poll(&writable, 1, 100);
            continue;
        }
        if (count < 0) {
            break;
        }
        written += count;
    }
}

} // namespace

void setPkexecPrompt(PkexecPrompt prompt)
{
    promptHandler = std::move(prompt);
}

bool runPkexecConversation(const QString &program,
                           const QStringList &arguments,
                           QString *error,
                           int timeoutMs)
{
    QByteArrayList argumentStorage{QFile::encodeName(program)};
    for (const QString &argument : arguments) {
        argumentStorage.append(argument.toLocal8Bit());
    }
    std::vector<char *> argv;
    argv.reserve(argumentStorage.size() + 1);
    for (QByteArray &argument : argumentStorage) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    int errorPipe[2];
    // Close-on-exec everywhere: another process launched while authentication
    // is pending must not inherit the conversation's descriptors. The child's
    // dup2 clears the flag on the copies it keeps.
    if (pipe2(errorPipe, O_CLOEXEC) != 0) {
        if (error) {
            *error = QStringLiteral("Could not start %1").arg(program);
        }
        return false;
    }
    int terminal = -1;
    const pid_t child = forkpty(&terminal, nullptr, nullptr, nullptr);
    if (child < 0) {
        close(errorPipe[0]);
        close(errorPipe[1]);
        if (error) {
            *error = QStringLiteral("Could not start %1").arg(program);
        }
        return false;
    }
    if (child == 0) {
        // The PTY stays the controlling terminal, where the authentication
        // agent talks; the program's own stderr goes to a pipe so error text
        // is not mixed into that conversation. stdout is not read by anyone.
        const int null = open("/dev/null", O_WRONLY);
        if (null >= 0) {
            dup2(null, STDOUT_FILENO);
            close(null);
        }
        dup2(errorPipe[1], STDERR_FILENO);
        close(errorPipe[0]);
        close(errorPipe[1]);
        // The C locale keeps the agent's prompts recognizable: a localized
        // PAM can end its password prompt with punctuation other than the
        // colon the prompt detection looks for.
        setenv("LC_ALL", "C", 1);
        execv(argv[0], argv.data());
        _exit(127);
    }
    close(errorPipe[1]);
    fcntl(terminal, F_SETFD, FD_CLOEXEC);
    setNonBlocking(terminal);
    setNonBlocking(errorPipe[0]);

    QByteArray errorOutput;
    QByteArray conversation;
    // With echo on, the terminal repeats a written reply back before the next
    // prompt; that repeat is not the agent talking, so it is stripped.
    QByteArray expectedEcho;
    QElapsedTimer terminalQuiet;
    terminalQuiet.start();
    QDeadlineTimer deadline(timeoutMs);
    bool terminalOpen = true;
    bool pipeOpen = true;
    bool exited = false;
    bool canceled = false;
    bool unanswerable = false;
    bool timedOut = false;
    int status = 0;
    while (!exited || terminalOpen || pipeOpen) {
        pollfd descriptors[2];
        nfds_t descriptorCount = 0;
        if (terminalOpen) {
            descriptors[descriptorCount++] = {terminal, POLLIN, 0};
        }
        if (pipeOpen) {
            descriptors[descriptorCount++] = {errorPipe[0], POLLIN, 0};
        }
        poll(descriptors, descriptorCount, 100);
        if (terminalOpen) {
            const qsizetype before = conversation.size();
            terminalOpen = drainDescriptor(terminal, &conversation);
            if (conversation.size() != before) {
                terminalQuiet.restart();
            }
        }
        if (pipeOpen) {
            pipeOpen = drainDescriptor(errorPipe[0], &errorOutput);
        }
        if (!exited && waitpid(child, &status, WNOHANG) == child) {
            exited = true;
            // Drain what is left, but only briefly: a root grandchild could
            // hold the pipe open indefinitely.
            deadline = QDeadlineTimer(1000);
        }
        if (!expectedEcho.isEmpty() && conversation.startsWith(expectedEcho)) {
            conversation.remove(0, expectedEcho.size());
            expectedEcho.clear();
        }
        if (!exited && !canceled && terminalOpen
            && waitingAtPrompt(conversation)
            && terminalQuiet.elapsed() >= promptQuietMs) {
            termios settings{};
            const bool echoOff = tcgetattr(terminal, &settings) == 0
                && (settings.c_lflag & ECHO) == 0;
            QString reply;
            if (!promptHandler) {
                unanswerable = true;
                canceled = true;
                kill(child, SIGTERM);
            } else if (promptHandler(terminalText(conversation), echoOff, &reply)) {
                writeReply(terminal, reply);
                if (!echoOff) {
                    expectedEcho = reply.toLocal8Bit() + "\r\n";
                }
                // The dialog blocks this loop for as long as the user thinks;
                // an answer must not land after the deadline it never saw.
                deadline = QDeadlineTimer(timeoutMs);
            } else {
                // Before authentication pkexec still runs as the invoking
                // user, so the signal is allowed to reach it.
                canceled = true;
                kill(child, SIGTERM);
            }
            conversation.clear();
        }
        if (deadline.hasExpired()) {
            if (!exited) {
                timedOut = true;
                // A delivered SIGKILL cannot be blocked, so a blocking reap
                // ends. After authentication pkexec runs as root and the
                // signal is refused; then only probe, accepting a zombie the
                // way the QProcess::kill it replaced did.
                if (kill(child, SIGKILL) == 0) {
                    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
                    }
                    exited = true;
                } else {
                    exited = waitpid(child, &status, WNOHANG) == child;
                }
            }
            break;
        }
    }
    close(terminal);
    close(errorPipe[0]);
    if (canceled) {
        if (error) {
            *error = unanswerable
                ? QStringLiteral("There is no way to ask for administrator authentication in this session.")
                : QStringLiteral("Administrator authentication was canceled.");
        }
        return false;
    }
    if (timedOut || !exited) {
        if (error) {
            *error = QStringLiteral("%1 timed out").arg(program);
        }
        return false;
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        return true;
    }
    if (error) {
        const QString text = QString::fromUtf8(errorOutput).trimmed();
        *error = text.isEmpty() ? QStringLiteral("%1 failed").arg(program) : text;
    }
    return false;
}

QString currentUserName()
{
    if (const passwd *pw = getpwuid(getuid())) {
        return QString::fromLocal8Bit(pw->pw_name);
    }
    return qEnvironmentVariable("USER");
}

bool userInGroup(const char *group, const QString &userName)
{
    const QByteArray user = userName.toLocal8Bit();
    const struct group *grp = getgrnam(group);
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
}

bool currentSessionInGroup(const char *group)
{
    const struct group *grp = getgrnam(group);
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
}

bool runProgram(const QString &program,
                const QStringList &arguments,
                QString *error,
                int timeoutMs)
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

bool runSetupHelper(const char *installedHelperPath,
                    const QStringList &companionFileNames,
                    HelperAction action,
                    QString *error)
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
    // Root never executes from a user-writable path. The installed libexec
    // path is root-owned and runs directly; a helper found anywhere else (an
    // AppImage mount, a build directory) is first copied to a root-owned
    // directory by pkexec'd /usr/bin/install — fixed tooling, so nothing a
    // user can rewrite runs as root — and the root-owned copy is what runs.
    // The two prompts this costs are honest: one names install, one the helper.
    QString helper = resolvedHelperPath(installedHelperPath);
    if (helper != QLatin1StringView(installedHelperPath)) {
        const QDir sourceDirectory = QFileInfo(helper).dir();
        const QString helperName = QFileInfo(helper).fileName();

        // The helper usually sits in the AppImage's FUSE mount, which only the
        // user who mounted it can read: pkexec's root gets EACCES from the
        // mount itself, whatever the file's own mode, so staging straight from
        // there fails with "cannot stat". Copy the helper and its companions
        // onto a normal filesystem first, as this unprivileged process which
        // can read the mount, so root can then read them to stage.
        QTemporaryDir readable;
        if (!readable.isValid()) {
            if (error) {
                *error = QStringLiteral("Could not create a temporary directory for the key helper.");
            }
            return false;
        }
        QStringList names{helperName};
        names.append(companionFileNames);
        QStringList sources;
        for (const QString &name : names) {
            const QString destination = readable.filePath(name);
            QFile::remove(destination);
            if (!QFile::copy(sourceDirectory.filePath(name), destination)) {
                if (error) {
                    *error = QStringLiteral("Could not read the bundled %1.").arg(name);
                }
                return false;
            }
            sources.append(destination);
        }

        QStringList stageArguments{QStringLiteral("/usr/bin/install"),
                                   QStringLiteral("-o"), QStringLiteral("root"),
                                   QStringLiteral("-g"), QStringLiteral("root"),
                                   QStringLiteral("-m"), QStringLiteral("0755"),
                                   QStringLiteral("-D"),
                                   QStringLiteral("-t"),
                                   QLatin1StringView(rootStageDirectory)};
        stageArguments.append(sources);
        if (!runPkexecConversation(pkexec, stageArguments, error, 5 * 60 * 1000)) {
            return false;
        }
        helper = QLatin1StringView(rootStageDirectory) + QLatin1Char('/') + helperName;
    }
    return runPkexecConversation(pkexec,
                      {helper,
                       action == HelperAction::Install ? QStringLiteral("--install")
                                                       : QStringLiteral("--remove"),
                       QStringLiteral("--user"),
                       user},
                      error,
                      5 * 60 * 1000);
}

} // namespace speecher::helpers
