#include "providers/ClaudeCredentials.h"

#include "core/CliToolDiscovery.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QTimeZone>

#include <cerrno>
#include <cstring>
#include <vector>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace speecher {

namespace {

constexpr int refreshStartupTimeoutMs = 250;
constexpr int refreshSessionTimeoutMs = 15000;
constexpr int refreshExitFallbackMs = 7000;

ClaudeCredentialResult readCredentials(const QString &path)
{
    ClaudeCredentialResult result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("Claude credentials not found at %1; run `claude /login` or `claude auth login`").arg(path);
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        result.error = QStringLiteral("Claude credentials file is not valid JSON");
        return result;
    }

    const QJsonObject oauth = doc.object().value(QStringLiteral("claudeAiOauth")).toObject();
    result.accessToken = oauth.value(QStringLiteral("accessToken")).toString();
    result.refreshToken = oauth.value(QStringLiteral("refreshToken")).toString();
    result.subscriptionType = oauth.value(QStringLiteral("subscriptionType")).toString();
    result.rateLimitTier = oauth.value(QStringLiteral("rateLimitTier")).toString();
    const qint64 expires = static_cast<qint64>(oauth.value(QStringLiteral("expiresAt")).toDouble());
    result.expiresAt = QDateTime::fromSecsSinceEpoch(expires / (expires > 9999999999LL ? 1000 : 1),
                                                     QTimeZone::UTC);
    for (const QJsonValue &scope : oauth.value(QStringLiteral("scopes")).toArray()) {
        result.scopes << scope.toString();
    }

    if (result.accessToken.isEmpty()) {
        result.error = QStringLiteral("Claude credentials do not contain claudeAiOauth.accessToken");
        return result;
    }
    if (result.expiresAt.isValid() && result.expiresAt <= QDateTime::currentDateTimeUtc()) {
        result.error = QStringLiteral("Claude login expired; run `claude /login` or `claude auth login`");
        return result;
    }

    result.ok = true;
    return result;
}

QString findClaudeExecutable()
{
    return CliToolDiscovery::claudeCodeExecutable();
}

#ifdef Q_OS_UNIX
QString lastSystemError(const char *operation)
{
    return QStringLiteral("%1: %2").arg(QString::fromUtf8(operation), QString::fromLocal8Bit(std::strerror(errno)));
}

bool writeAll(int fd, QByteArrayView data)
{
    while (!data.empty()) {
        const ssize_t written = ::write(fd, data.data(), static_cast<size_t>(data.size()));
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        data = data.sliced(static_cast<qsizetype>(written));
    }
    return true;
}

bool waitForProcess(pid_t pid, int timeoutMs, int *status)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        const pid_t result = ::waitpid(pid, status, WNOHANG);
        if (result == pid) {
            return true;
        }
        if (result < 0 && errno != EINTR) {
            return true;
        }
        ::usleep(50000);
    }
    return false;
}

bool launchInteractiveClaudeRefresh(const QString &executable, QString *error)
{
    const int masterFd = ::posix_openpt(O_RDWR | O_NOCTTY);
    if (masterFd < 0) {
        if (error) {
            *error = QStringLiteral("Could not create Claude refresh terminal; %1").arg(lastSystemError("posix_openpt"));
        }
        return false;
    }

    if (::grantpt(masterFd) != 0 || ::unlockpt(masterFd) != 0) {
        if (error) {
            *error = QStringLiteral("Could not prepare Claude refresh terminal; %1").arg(lastSystemError("grantpt/unlockpt"));
        }
        ::close(masterFd);
        return false;
    }

    char *slaveName = ::ptsname(masterFd);
    if (!slaveName) {
        if (error) {
            *error = QStringLiteral("Could not locate Claude refresh terminal; %1").arg(lastSystemError("ptsname"));
        }
        ::close(masterFd);
        return false;
    }

    const pid_t child = ::fork();
    if (child < 0) {
        if (error) {
            *error = QStringLiteral("Could not start Claude refresh session; %1").arg(lastSystemError("fork"));
        }
        ::close(masterFd);
        return false;
    }

    if (child == 0) {
        ::setsid();
        const int slaveFd = ::open(slaveName, O_RDWR);
        if (slaveFd < 0) {
            _exit(127);
        }
        termios terminal{};
        if (::tcgetattr(slaveFd, &terminal) == 0) {
            terminal.c_lflag &= ~ECHO;
            ::tcsetattr(slaveFd, TCSANOW, &terminal);
        }
        ::ioctl(slaveFd, TIOCSCTTY, 0);
        ::dup2(slaveFd, STDIN_FILENO);
        ::dup2(slaveFd, STDOUT_FILENO);
        ::dup2(slaveFd, STDERR_FILENO);
        if (slaveFd > STDERR_FILENO) {
            ::close(slaveFd);
        }
        ::close(masterFd);

        std::vector<QByteArray> argvStorage{
            QFile::encodeName(executable),
            QByteArrayLiteral("--tools"),
            QByteArray(),
            QByteArrayLiteral("--permission-mode"),
            QByteArrayLiteral("dontAsk"),
            QByteArrayLiteral("--no-chrome"),
        };
        std::vector<char *> argv;
        argv.reserve(argvStorage.size() + 1);
        for (QByteArray &arg : argvStorage) {
            argv.push_back(arg.data());
        }
        argv.push_back(nullptr);
        ::execv(argv.front(), argv.data());
        _exit(127);
    }

    const int flags = ::fcntl(masterFd, F_GETFL, 0);
    if (flags >= 0) {
        ::fcntl(masterFd, F_SETFL, flags | O_NONBLOCK);
    }

    QElapsedTimer timer;
    timer.start();
    bool promptSent = false;
    bool exitSent = false;
    qint64 promptSentAt = -1;
    QByteArray output;
    int status = 0;
    while (timer.elapsed() < refreshSessionTimeoutMs) {
        if (::waitpid(child, &status, WNOHANG) == child) {
            ::close(masterFd);
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                return true;
            }
            if (error) {
                *error = QStringLiteral("Claude refresh session exited unsuccessfully");
            }
            return false;
        }

        pollfd pfd{masterFd, static_cast<short>(POLLIN | POLLOUT), 0};
        const int polled = ::poll(&pfd, 1, 100);
        if (polled < 0 && errno != EINTR) {
            break;
        }
        if (polled > 0 && (pfd.revents & POLLIN)) {
            char buffer[1024];
            const ssize_t readCount = ::read(masterFd, buffer, sizeof(buffer));
            if (readCount > 0) {
                output.append(buffer, static_cast<qsizetype>(readCount));
            }
        }

        if (!promptSent && timer.elapsed() >= refreshStartupTimeoutMs) {
            promptSent = writeAll(masterFd, QByteArrayLiteral("Reply OK only.\n"));
            promptSentAt = timer.elapsed();
        } else if (promptSent && !exitSent
                   && (output.contains("OK") || timer.elapsed() - promptSentAt >= refreshExitFallbackMs)) {
            exitSent = writeAll(masterFd, QByteArrayLiteral("/exit\n"));
        }
    }

    ::kill(child, SIGTERM);
    if (!waitForProcess(child, 1000, &status)) {
        ::kill(child, SIGKILL);
        waitForProcess(child, 1000, &status);
    }
    ::close(masterFd);
    if (error) {
        const QString summary = QString::fromUtf8(output.left(240)).simplified();
        *error = summary.isEmpty()
            ? QStringLiteral("Timed out refreshing Claude login with an interactive Claude session")
            : QStringLiteral("Timed out refreshing Claude login with an interactive Claude session: %1").arg(summary);
    }
    return false;
}
#else
bool launchInteractiveClaudeRefresh(const QString &, QString *error)
{
    if (error) {
        *error = QStringLiteral("Interactive Claude login refresh is only supported on Unix-like systems");
    }
    return false;
}
#endif

bool refreshClaudeAuth(QString *error)
{
    const QString executable = findClaudeExecutable();
    if (executable.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Could not find Claude Code; install it and ensure `claude` is on PATH");
        }
        return false;
    }

    return launchInteractiveClaudeRefresh(executable, error);
}

} // namespace

QString ClaudeCredentials::installedVersion()
{
    const QString executable = findClaudeExecutable();
    if (executable.isEmpty()) {
        return {};
    }

    const QFileInfo executableInfo(executable);
    static const QRegularExpression versionPattern(QStringLiteral("\\b\\d+\\.\\d+\\.\\d+(?:[-+][A-Za-z0-9._-]+)?\\b"));
    const QRegularExpressionMatch pathMatch = versionPattern.match(executableInfo.fileName());
    if (pathMatch.hasMatch()) {
        return pathMatch.captured(0);
    }

    QProcess process;
    process.setProgram(executable);
    process.setArguments({QStringLiteral("--version")});
    process.start();
    if (!process.waitForStarted(1000)) {
        return {};
    }
    if (!process.waitForFinished(2000)) {
        process.kill();
        process.waitForFinished(1000);
        return {};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return {};
    }

    const QString output = QString::fromUtf8(process.readAllStandardOutput() + process.readAllStandardError());
    const QRegularExpressionMatch outputMatch = versionPattern.match(output);
    return outputMatch.hasMatch() ? outputMatch.captured(0) : QString();
}

ClaudeCredentialResult ClaudeCredentials::load(const QString &path, bool refreshExpired)
{
    ClaudeCredentialResult result = readCredentials(path);
    if (result.ok || !refreshExpired || !result.expiresAt.isValid()
        || result.expiresAt > QDateTime::currentDateTimeUtc()) {
        return result;
    }

    QString refreshError;
    if (!refreshClaudeAuth(&refreshError)) {
        result.error = refreshError;
        return result;
    }

    ClaudeCredentialResult refreshed = readCredentials(path);
    if (!refreshed.ok) {
        refreshed.error = QStringLiteral("Claude login refresh did not produce valid credentials; %1").arg(refreshed.error);
    }
    return refreshed;
}

bool ClaudeCredentials::requiresRefresh(const QString &path)
{
    const ClaudeCredentialResult result = readCredentials(path);
    return !result.ok && result.expiresAt.isValid()
        && result.expiresAt <= QDateTime::currentDateTimeUtc();
}

} // namespace speecher
