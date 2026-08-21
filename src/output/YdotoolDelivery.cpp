#include "output/YdotoolDelivery.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTextStream>

#include <limits>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace speecher {

namespace {

constexpr qint64 duplicateWindowMs = 2500;
constexpr int keyDelayMs = 1;
constexpr int keyHoldMs = 2;
constexpr int shortcutKeyDelayMs = 2;
constexpr int modifierTimeoutMs = 500;
constexpr int shortcutTimeoutMs = 1000;
constexpr int typeBaseTimeoutMs = 2000;
constexpr int typeSlackPerCharacterMs = 5;

QString runtimeDirectory()
{
    QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtime.isEmpty()) {
        runtime = QDir::tempPath();
    }
    QDir().mkpath(runtime);
    return runtime;
}

QString userToken()
{
#ifdef Q_OS_UNIX
    return QString::number(getuid());
#else
    return qEnvironmentVariable("USER", qEnvironmentVariable("USERNAME", QStringLiteral("user")));
#endif
}

QString deliveryStatePath()
{
    return runtimeDirectory() + QStringLiteral("/speecher-ydotool-delivery-%1.state").arg(userToken());
}

QString deliveryLockPath()
{
    return runtimeDirectory() + QStringLiteral("/speecher-ydotool-delivery-%1.lock").arg(userToken());
}

QString textHash(const QString &text)
{
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
}

bool wasRecentlyDelivered(const QString &text)
{
    QLockFile lock(deliveryLockPath());
    lock.setStaleLockTime(5000);
    if (!lock.tryLock(250)) {
        return false;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QString currentHash = textHash(text);

    QFile state(deliveryStatePath());
    if (state.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream stream(&state);
        const qint64 previousTime = stream.readLine().toLongLong();
        const QString previousHash = stream.readLine().trimmed();
        state.close();
        if (previousHash == currentHash && now - previousTime >= 0 && now - previousTime < duplicateWindowMs) {
            return true;
        }
    }

    return false;
}

void recordSuccessfulDelivery(const QString &text)
{
    QLockFile lock(deliveryLockPath());
    lock.setStaleLockTime(5000);
    if (!lock.tryLock(250)) {
        return;
    }

    QFile state(deliveryStatePath());
    if (state.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QTextStream stream(&state);
        stream << QDateTime::currentMSecsSinceEpoch() << '\n' << textHash(text) << '\n';
    }
}

bool runYdotool(const QString &executable,
                const QProcessEnvironment &env,
                const QStringList &arguments,
                int timeoutMs,
                QString *error)
{
    QProcess process;
    process.setProcessEnvironment(env);
    process.start(executable, arguments);
    if (!process.waitForStarted(1000)) {
        if (error) {
            *error = QStringLiteral("Could not start ydotool");
        }
        return false;
    }

    if (process.waitForFinished(timeoutMs)
        && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
        return true;
    }

    process.kill();
    const QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
    if (error) {
        *error = stderrText.isEmpty() ? QStringLiteral("ydotool failed") : QStringLiteral("ydotool failed: %1").arg(stderrText);
    }
    return false;
}

bool releaseModifierKeys(const QString &executable, const QProcessEnvironment &env)
{
    QString ignored;
    return runYdotool(executable,
                      env,
                      {QStringLiteral("key"),
                       QStringLiteral("--key-delay=%1").arg(shortcutKeyDelayMs),
                       QStringLiteral("29:0"),
                       QStringLiteral("97:0"),
                       QStringLiteral("42:0"),
                       QStringLiteral("54:0")},
                      modifierTimeoutMs,
                      &ignored);
}

} // namespace

YdotoolDelivery::YdotoolDelivery(QObject *parent)
    : QObject(parent)
{
}

bool YdotoolDelivery::isAvailable()
{
    return !QStandardPaths::findExecutable(QStringLiteral("ydotool")).isEmpty();
}

QString YdotoolDelivery::socketPath()
{
    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    return runtime.isEmpty() ? QString() : runtime + QStringLiteral("/.ydotool_socket");
}

QStringList YdotoolDelivery::commandArguments(const QString &text)
{
    const QString typedText = withoutTrailingWhitespace(text);
    return {
        QStringLiteral("type"),
        QStringLiteral("--key-delay=%1").arg(keyDelayMs),
        QStringLiteral("--key-hold=%1").arg(keyHoldMs),
        QStringLiteral("--escape=0"),
        QStringLiteral("--"),
        typedText,
    };
}

QStringList YdotoolDelivery::pasteShortcutArguments(PasteMethod method)
{
    QStringList arguments{
        QStringLiteral("key"),
        QStringLiteral("--key-delay=%1").arg(shortcutKeyDelayMs),
        QStringLiteral("29:1"),
    };
    if (method == PasteMethod::TerminalPaste) {
        arguments << QStringLiteral("42:1");
    }
    arguments << QStringLiteral("47:1") << QStringLiteral("47:0");
    if (method == PasteMethod::TerminalPaste) {
        arguments << QStringLiteral("42:0");
    }
    arguments << QStringLiteral("29:0");
    return arguments;
}

QStringList YdotoolDelivery::copyShortcutArguments(PasteMethod method)
{
    QStringList arguments{
        QStringLiteral("key"),
        QStringLiteral("--key-delay=%1").arg(shortcutKeyDelayMs),
        QStringLiteral("29:1"),
    };
    if (method == PasteMethod::TerminalPaste) {
        arguments << QStringLiteral("42:1");
    }
    arguments << QStringLiteral("46:1") << QStringLiteral("46:0");
    if (method == PasteMethod::TerminalPaste) {
        arguments << QStringLiteral("42:0");
    }
    arguments << QStringLiteral("29:0");
    return arguments;
}

QString YdotoolDelivery::withoutTrailingWhitespace(const QString &text)
{
    QString cleaned = text;
    while (!cleaned.isEmpty() && cleaned.back().isSpace()) {
        cleaned.chop(1);
    }
    return cleaned;
}

bool YdotoolDelivery::type(const QString &text, QString *error)
{
    const QString typedText = withoutTrailingWhitespace(text);
    if (typedText.isEmpty()) {
        return true;
    }

    if (wasRecentlyDelivered(typedText)) {
        return true;
    }

    const QString executable = QStandardPaths::findExecutable(QStringLiteral("ydotool"));
    if (executable.isEmpty()) {
        if (error) {
            *error = QStringLiteral("ydotool is not installed");
        }
        return false;
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString socket = socketPath();
    if (!socket.isEmpty()) {
        env.insert(QStringLiteral("YDOTOOL_SOCKET"), socket);
    }

    releaseModifierKeys(executable, env);
    const int typeTimeoutMs = int(qMin<qsizetype>(
        std::numeric_limits<int>::max(),
        typeBaseTimeoutMs
            + typedText.size() * (keyDelayMs + keyHoldMs + typeSlackPerCharacterMs)));
    if (!runYdotool(executable, env, commandArguments(typedText), typeTimeoutMs, error)) {
        releaseModifierKeys(executable, env);
        return false;
    }
    recordSuccessfulDelivery(typedText);
    releaseModifierKeys(executable, env);
    return true;
}

bool YdotoolDelivery::pasteFromClipboard(const QString &text, PasteMethod method, QString *error)
{
    if (text.isEmpty()) {
        return true;
    }

    if (wasRecentlyDelivered(text)) {
        return true;
    }

    const QString executable = QStandardPaths::findExecutable(QStringLiteral("ydotool"));
    if (executable.isEmpty()) {
        if (error) {
            *error = QStringLiteral("ydotool is not installed");
        }
        return false;
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString socket = socketPath();
    if (!socket.isEmpty()) {
        env.insert(QStringLiteral("YDOTOOL_SOCKET"), socket);
    }

    releaseModifierKeys(executable, env);
    if (!runYdotool(executable, env, pasteShortcutArguments(method), shortcutTimeoutMs, error)) {
        releaseModifierKeys(executable, env);
        return false;
    }
    recordSuccessfulDelivery(text);
    releaseModifierKeys(executable, env);
    return true;
}

bool YdotoolDelivery::copySelection(PasteMethod method, QString *error)
{
    const QString executable = QStandardPaths::findExecutable(QStringLiteral("ydotool"));
    if (executable.isEmpty()) {
        if (error) {
            *error = QStringLiteral("ydotool is not installed");
        }
        return false;
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString socket = socketPath();
    if (!socket.isEmpty()) {
        env.insert(QStringLiteral("YDOTOOL_SOCKET"), socket);
    }

    releaseModifierKeys(executable, env);
    if (!runYdotool(executable, env, copyShortcutArguments(method), shortcutTimeoutMs, error)) {
        releaseModifierKeys(executable, env);
        return false;
    }
    releaseModifierKeys(executable, env);
    return true;
}

} // namespace speecher
