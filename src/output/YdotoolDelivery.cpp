#include "output/YdotoolDelivery.h"

#include "output/ProcessRunner.h"

#include <QCoreApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>

#include <limits>
#include <utility>

namespace speecher {

namespace {

constexpr int keyDelayMs = 1;
constexpr int keyHoldMs = 2;
constexpr int shortcutKeyDelayMs = 2;
constexpr int modifierTimeoutMs = 500;
constexpr int shortcutTimeoutMs = 1000;
constexpr int typeBaseTimeoutMs = 2000;
constexpr int typeSlackPerCharacterMs = 5;

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

// Lets go of both Ctrl and both Shift keys, so a held one cannot turn the
// keystrokes into another shortcut.
QStringList modifierReleaseArguments()
{
    return {QStringLiteral("key"),
            QStringLiteral("--key-delay=%1").arg(shortcutKeyDelayMs),
            QStringLiteral("29:0"),
            QStringLiteral("97:0"),
            QStringLiteral("42:0"),
            QStringLiteral("54:0")};
}

bool releaseModifierKeys(const QString &executable, const QProcessEnvironment &env)
{
    QString ignored;
    return runYdotool(executable, env, modifierReleaseArguments(), modifierTimeoutMs, &ignored);
}

} // namespace

YdotoolDelivery::YdotoolDelivery(QObject *parent)
    : QObject(parent)
{
}

YdotoolDelivery::~YdotoolDelivery()
{
    if (!m_pasteUnderWay) {
        return;
    }
    // Taken out first: stopping answers at once, and this still counts as
    // alive until QObject's own destructor.
    const PasteUnderWay paste = *std::exchange(m_pasteUnderWay, std::nullopt);
    paste.stop();
    // The application's: this is gone before the release answers. A killed
    // keystroke may have left Ctrl down.
    runProcess(paste.release, QCoreApplication::instance(), [](const ProcessResult &) {});
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
    releaseModifierKeys(executable, env);
    return true;
}

void YdotoolDelivery::pasteFromClipboard(const QString &text,
                                         PasteMethod method,
                                         const std::function<bool()> &clearToInject,
                                         std::function<void(bool, const QString &)> done)
{
    if (text.isEmpty()) {
        done(true, {});
        return;
    }

    const QString executable = QStandardPaths::findExecutable(QStringLiteral("ydotool"));
    if (executable.isEmpty()) {
        done(false, QStringLiteral("ydotool is not installed"));
        return;
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString socket = socketPath();
    if (!socket.isEmpty()) {
        env.insert(QStringLiteral("YDOTOOL_SOCKET"), socket);
    }
    const auto ydotool = [&executable, &env](const QStringList &arguments, int timeoutMs) {
        return ProcessCommand{.executable = executable,
                              .tool = QStringLiteral("ydotool"),
                              .arguments = arguments,
                              .timeoutMs = timeoutMs,
                              .environment = env};
    };
    ProcessCommand release = ydotool(modifierReleaseArguments(), modifierTimeoutMs);
    // A modifier left down would turn the person's next keys into shortcuts.
    release.finishesAtShutdown = true;
    const ProcessCommand paste = ydotool(pasteShortcutArguments(method), shortcutTimeoutMs);
    runProcess(release, this, [this, clearToInject, paste, release, done](const ProcessResult &) {
        // The release took a turn of the event loop or more; the user may
        // have switched windows meanwhile. Last check before the keystroke,
        // because a paste into the wrong window cannot be undone.
        if (clearToInject && !clearToInject()) {
            done(false, QStringLiteral("The active window changed or could not be verified"));
            return;
        }
        m_pasteUnderWay = PasteUnderWay{{}, release};
        // The paste's own answer, once the modifiers are released after it.
        auto stop = runProcess(paste, this, [this, release, done](const ProcessResult &pasted) {
            // Nothing under way: the destructor stopped it and releases.
            if (!m_pasteUnderWay) {
                return;
            }
            m_pasteUnderWay.reset();
            runProcess(release, this, [pasted, done](const ProcessResult &) { done(pasted.ok, pasted.error); });
        });
        // A command that failed to start has answered already.
        if (m_pasteUnderWay) {
            m_pasteUnderWay->stop = std::move(stop);
        }
    });
}

} // namespace speecher
