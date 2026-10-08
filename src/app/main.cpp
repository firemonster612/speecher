#include "app/ApplicationController.h"
#ifdef Q_OS_LINUX
#include "app/AppImageUpdater.h"
#endif
#include "app/UpdateController.h"
#include "app/CommandLine.h"
#include "app/PlatformComposition.h"
#include "app/ProviderSetup.h"
#include "app/ProvidersCommand.h"
#include "core/SettingsStore.h"
#include "core/settings/SettingsKeys.h"
#include "providers/LocalModelStore.h"
#include "providers/ProviderRegistry.h"
#ifndef SPEECHER_WITH_WINUI
#include "frontend/qt/QtFrontEnd.h"
#include "ui/Theme.h"
#endif

#ifdef Q_OS_LINUX
#include "ui/HostStylePlugin.h"
#include "platform/LinuxStyleChoice.h"
#endif

#ifdef SPEECHER_WITH_SWIFT_UI
#include "frontend/mac/MacFrontEnd.h"
#endif
#ifdef SPEECHER_WITH_WINUI
#include "frontend/win/WinFrontEnd.h"
#include "frontend/win/WinUiHost.h"
#include <windows.h>
#endif

#include <QApplication>
#include <QDateTime>
#ifdef Q_OS_WIN
#include <QDeadlineTimer>
#include <QThread>
#endif
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QIcon>
#include <QSettings>
#include <QStandardPaths>
#ifdef Q_OS_LINUX
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#endif
#include <QTextStream>
#include <QTimer>
#include <QMutex>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <optional>
#include <thread>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#include "helpers/ConsoleLauncher.h"
#else
#include <termios.h>
#include <unistd.h>
#endif

using namespace speecher;

static QFile *g_logFile = nullptr;
static QMutex g_logMutex;

static void messageHandler(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    QMutexLocker locker(&g_logMutex);
    if (!g_logFile || !g_logFile->isOpen()) {
        return;
    }
    const char *level = "info";
    if (type == QtDebugMsg) {
        level = "debug";
    } else if (type == QtWarningMsg) {
        level = "warning";
    } else if (type == QtCriticalMsg) {
        level = "critical";
    } else if (type == QtFatalMsg) {
        level = "fatal";
    }
    QTextStream stream(g_logFile);
    stream << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
           << ' ' << level << ' ' << message << '\n';
    stream.flush();
}

static QString installLogHandler()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (dir.isEmpty()) {
        dir = QDir::homePath() + QStringLiteral("/.cache/speecher");
    }
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/speecher.log");
    g_logFile = new QFile(path);
    if (!g_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        delete g_logFile;
        g_logFile = nullptr;
    }
    qInstallMessageHandler(messageHandler);
    qInfo().noquote() << "speecher log started path=" + path;
    return path;
}

static void migrateSettings()
{
    constexpr auto oldOrganization = "local.speecher";
    QSettings newSettings(QString::fromLatin1(SettingsKeys::Organization),
                          QString::fromLatin1(SettingsKeys::Application));
    QSettings oldSettings(QString::fromLatin1(oldOrganization),
                          QString::fromLatin1(SettingsKeys::Application));
    QString error;
    if (!migrateSettingsIdentity(newSettings, oldSettings, &error)) {
        qWarning().noquote() << error;
    }
    migrateRefinementModels(newSettings);
    migrateOutputMethod(newSettings);
}

static QStringList commandLineArguments(int argc, char **argv)
{
    QStringList arguments;
    arguments.reserve(argc);
    for (int index = 0; index < argc; ++index) {
        arguments << QString::fromLocal8Bit(argv[index]);
    }
    return arguments;
}

#ifdef Q_OS_WIN
// Whether a standard stream was left unset, rather than given a file, pipe or
// console.
static bool unredirected(DWORD stream)
{
    const HANDLE handle = GetStdHandle(stream);
    return handle == nullptr || handle == INVALID_HANDLE_VALUE || GetFileType(handle) == FILE_TYPE_UNKNOWN;
}

// Whether the CRT bound no handle to a standard stream at startup. It binds
// one to a file, pipe or NUL, but not to an unset standard handle or a console
// handle this process could not use yet, and marks those -2: the stream's
// descriptor, or the handle behind it.
static bool unbound(FILE *stream)
{
    const int fd = _fileno(stream);
    return fd < 0 || _get_osfhandle(fd) < 0;
}

// Reopens a standard stream on path and points the standard handle at the new
// handle to match, for code that asks for the standard handle rather than
// writing the stream. freopen closes the handle the stream had, if any.
static void reopen(FILE *stream, const char *path, const char *mode, DWORD standardHandle)
{
    if (std::freopen(path, mode, stream) == nullptr) {
        return;
    }
    SetStdHandle(standardHandle, reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stream))));
}

// Speecher is a GUI-subsystem program, so a command-line run starts with no
// console. Borrow the one it was started from, if any, and point stdout and
// stderr at it unless they already go to a file or pipe. cmd.exe does not
// wait for a GUI-subsystem program, so its prompt can come back first.
//
// A console handle the launcher hands over is unusable until AttachConsole
// reconnects it, by which point the CRT has already left those streams
// unbound, so cout and cerr write nowhere. Reopen them on the console. They
// are opened for reading too, since GetConsoleMode, which stderrIsTerminal
// asks, needs read access.
static void attachParentConsole()
{
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        return;
    }
    if (unbound(stdout)) {
        reopen(stdout, "CONOUT$", "w+", STD_OUTPUT_HANDLE);
    }
    if (unbound(stderr)) {
        reopen(stderr, "CONOUT$", "w+", STD_ERROR_HANDLE);
    }
    std::cout.clear();
    std::cerr.clear();
    // TEMP diagnostics for #239, removed before merge.
    DWORD errMode = 0;
    QFile diag(QDir::temp().filePath(QStringLiteral("speecher-condiag.txt")));
    if (diag.open(QIODevice::Append | QIODevice::Text)) {
        diag.write(QStringLiteral("out_fd=%1 err_fd=%2 err_console_mode=%3\n")
                       .arg(_fileno(stdout))
                       .arg(_fileno(stderr))
                       .arg(GetConsoleMode(GetStdHandle(STD_ERROR_HANDLE), &errMode) ? 1 : 0)
                       .toLatin1());
    }
}

// A window or daemon run outlives the console it was started from, and
// closing that console ends every process attached to it. It also lets go of
// the standard handles it was given: a caller reading a redirected stdout,
// such as `$out = speecher --daemon`, waits until every writer closes the
// pipe. speecher.com, if that is what started this, stops waiting for it, but
// only once this has left the console: a shell that exits with the launcher
// closes it.
//
// The streams move to NUL before FreeConsole, so none of them is left, even
// briefly, writing to a console this process has left.
static void detachParentConsole()
{
    reopen(stdin, "NUL", "r", STD_INPUT_HANDLE);
    reopen(stdout, "NUL", "w", STD_OUTPUT_HANDLE);
    reopen(stderr, "NUL", "w", STD_ERROR_HANDLE);
    FreeConsole();
    std::cout.clear();
    std::cerr.clear();
    const HANDLE launcherWait = OpenEventW(EVENT_MODIFY_STATE, FALSE,
                                           consoleDetachedEventName(GetCurrentProcessId()).c_str());
    if (launcherWait) {
        SetEvent(launcherWait);
        CloseHandle(launcherWait);
    }
}

// Whether stderr is a console that takes the escape codes that rewrite the
// progress line; NUL and files are not, and neither is a console that
// refuses virtual terminal processing.
static bool stderrIsTerminal()
{
    const HANDLE handle = GetStdHandle(STD_ERROR_HANDLE);
    DWORD mode = 0;
    return GetConsoleMode(handle, &mode)
        && SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}
#else
static void attachParentConsole() {}
static void detachParentConsole() {}

static bool stderrIsTerminal()
{
    return isatty(fileno(stderr));
}
#endif

#ifdef Q_OS_WIN
// Whether stdin is a console, which `transcribe -` would only wait on. A
// GUI-subsystem program is not handed the console as its stdin, so an unset
// one is the console too.
static bool stdinIsTerminal()
{
    DWORD mode = 0;
    return unredirected(STD_INPUT_HANDLE) || GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode);
}

static BOOL WINAPI removeStdinSpoolOnConsoleEvent(DWORD)
{
    removeStdinSpool();
    // The default handler then ends the process.
    return FALSE;
}
#else
static bool stdinIsTerminal()
{
    return isatty(STDIN_FILENO);
}

// One the parent ignores, as nohup does SIGHUP, stays ignored.
static void handleUnlessIgnored(int signal, void (*handler)(int))
{
    if (std::signal(signal, handler) == SIG_IGN) {
        std::signal(signal, SIG_IGN);
    }
}

static void removeStdinSpoolAndEnd(int signal)
{
    removeStdinSpool();
    std::signal(signal, SIG_DFL);
    std::raise(signal);
}
#endif

// A signal ends `transcribe -` without unwinding, so these remove the spooled
// audio first; SIGPIPE covers a reader that quit early, as `| true` does. On
// Windows a console event's removal is best effort, see removeStdinSpool.
static void installStdinSpoolRemoval()
{
#ifdef Q_OS_WIN
    SetConsoleCtrlHandler(removeStdinSpoolOnConsoleEvent, TRUE);
#else
    for (const int signal : {SIGINT, SIGTERM, SIGHUP, SIGPIPE}) {
        handleUnlessIgnored(signal, removeStdinSpoolAndEnd);
    }
#endif
}

// Set by Ctrl-C, or Enter at a terminal, while `speecher listen` records.
static std::atomic<bool> g_listenStopRequested{false};

#ifdef Q_OS_WIN
static BOOL WINAPI stopListeningOnCtrlC(DWORD event)
{
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT) {
        return FALSE;
    }
    // The second one ends the process as usual.
    return !g_listenStopRequested.exchange(true);
}

// Reads stdin a byte at a time when it is a console. A GUI-subsystem program
// is not handed the console attachParentConsole borrowed as its stdin, so that
// one is opened by name; a launcher that passes its own handles hands it over.
// Empty for a file, a pipe or no console.
static std::function<bool(char *)> openTerminalInput()
{
    const HANDLE input = unredirected(STD_INPUT_HANDLE)
        ? CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                      OPEN_EXISTING, 0, nullptr)
        : GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (!GetConsoleMode(input, &mode)) {
        return {};
    }
    return [input](char *byte) {
        DWORD read = 0;
        return ReadFile(input, byte, 1, &read, nullptr) && read == 1;
    };
}
#else
// The terminal's settings from before listen changed them, put back on every
// way out. Saved before g_terminalChanged is set.
static termios g_savedTerminal;
static std::atomic<bool> g_terminalChanged{false};

// Safe in a signal handler.
static void restoreTerminal()
{
    if (g_terminalChanged) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_savedTerminal);
    }
}

static void restoreTerminalAndEnd(int signal)
{
    restoreTerminal();
    std::signal(signal, SIG_DFL);
    std::raise(signal);
}

static void stopListeningOnSigint(int signal)
{
    // The second one ends the process as usual.
    if (g_listenStopRequested.exchange(true)) {
        restoreTerminalAndEnd(signal);
    }
}

// Reads stdin a byte at a time when it is the terminal this process is in the
// foreground of; a background run would be stopped for reading it. Turns off
// the terminal's signal keys meanwhile: Ctrl-C's SIGINT goes to the whole
// foreground process group, so the shell around `x="$(speecher listen)"`
// would drop the line along with what was said. Empty otherwise.
static std::function<bool(char *)> openTerminalInput()
{
    // Not a terminal, or one this process is in the background of.
    if (tcgetpgrp(STDIN_FILENO) != getpgrp() || tcgetattr(STDIN_FILENO, &g_savedTerminal) != 0) {
        return {};
    }
    g_terminalChanged = true;
    std::atexit(restoreTerminal);
    // SIGABRT covers an uncaught exception.
    for (const int signal : {SIGTERM, SIGHUP, SIGABRT}) {
        handleUnlessIgnored(signal, restoreTerminalAndEnd);
    }
    termios terminal = g_savedTerminal;
    // Without canonical mode Ctrl-C needs no Enter after it to be read.
    terminal.c_lflag &= ~(ISIG | ICANON);
    terminal.c_cc[VMIN] = 1;
    terminal.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &terminal);
    return [](char *byte) { return ::read(STDIN_FILENO, byte, 1) == 1; };
}
#endif

// Ctrl-C and Enter end the recording rather than the process, so what was
// said is still transcribed. Only a terminal's stdin is read for Enter: piped
// input, such as a surrounding `while read` loop's, is left alone. It is read
// raw on a thread of its own, since the event loop cannot watch a Windows
// console. Returns whether Enter is read.
static bool installListenStopHandlers()
{
#ifdef Q_OS_WIN
    // A parent may have left Ctrl+C ignored, which would hide it from the handler.
    SetConsoleCtrlHandler(nullptr, FALSE);
    SetConsoleCtrlHandler(stopListeningOnCtrlC, TRUE);
#else
    std::signal(SIGINT, stopListeningOnSigint);
#endif
    const std::function<bool(char *)> readByte = openTerminalInput();
    if (!readByte) {
        return false;
    }
    // Read on after a stop, so a terminal's Ctrl-C can still end a slow
    // transcription.
    std::thread([readByte] {
        char byte = 0;
        while (readByte(&byte)) {
            if (byte == '\n' || byte == '\r') {
                g_listenStopRequested = true;
            }
#ifndef Q_OS_WIN
            // Ctrl-C, which the terminal now hands over as a byte. The second
            // one ends the process, with the status a shell gives SIGINT.
            constexpr char kCtrlC = '\x03';
            if (byte == kCtrlC && g_listenStopRequested.exchange(true)) {
                restoreTerminal();
                std::_Exit(128 + SIGINT);
            }
#endif
        }
    }).detach();
    return true;
}

// macOS may ask the first time, and answers once the event loop runs.
static bool microphoneAccessGranted(const PlatformComposition &platform, QObject *context)
{
    std::optional<bool> granted;
    QEventLoop loop;
    platform.requestMicrophoneAccess(context, [&](bool answer) {
        granted = answer;
        loop.quit();
    });
    if (!granted) {
        loop.exec();
    }
    return *granted;
}

#ifdef Q_OS_LINUX
static QString kdeWidgetStyle()
{
    const QStringList paths = QStandardPaths::locateAll(
        QStandardPaths::GenericConfigLocation,
        QStringLiteral("kdeglobals"));
    for (const QString &path : paths) {
        QSettings kdeglobals(path, QSettings::IniFormat);
        if (kdeglobals.contains(QStringLiteral("KDE/widgetStyle"))) {
            return kdeglobals.value(QStringLiteral("KDE/widgetStyle")).toString();
        }
    }
    return {};
}

static void applyHostWidgetStyle(const QString &applicationTheme)
{
    const QString currentStyle = qApp->style()->objectName();
    const LinuxStyleChoice choice = chooseLinuxStyle(
        qEnvironmentVariable("QT_STYLE_OVERRIDE"),
        kdeWidgetStyle(),
        qEnvironmentVariable("XDG_CURRENT_DESKTOP"),
        qEnvironmentVariable("QT_QPA_PLATFORMTHEME"),
        currentStyle,
        QStyleFactory::keys(),
        applicationTheme,
        qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark);

    // The desktop asked for a style this Qt does not carry: try the plugin
    // installed on the system before settling for the fallback. Qt refuses a
    // plugin built for a newer Qt than the one running, so this is best effort.
    if (!choice.requested.isEmpty()
        && choice.chosen.compare(choice.requested, Qt::CaseInsensitive) != 0) {
        QStringList attempts;
        if (QStyle *host = loadHostStyle(choice.requested, hostStylePluginDirs(), &attempts)) {
            QApplication::setStyle(host);
            qInfo().noquote() << "widget style requested=" + choice.requested
                                    + " chosen=" + qApp->style()->objectName() + " (system plugin)";
            return;
        }
        for (const QString &attempt : attempts) {
            qInfo().noquote() << "system style plugin " + attempt;
        }
    }
    if (currentStyle.compare(choice.chosen, Qt::CaseInsensitive) != 0
        && !QApplication::setStyle(choice.chosen)) {
        qWarning().noquote() << "Could not load widget style " + choice.chosen;
        if (currentStyle.compare(choice.fallback, Qt::CaseInsensitive) != 0
            && choice.chosen.compare(choice.fallback, Qt::CaseInsensitive) != 0
            && !QApplication::setStyle(choice.fallback)) {
            qWarning().noquote() << "Could not load fallback widget style " + choice.fallback;
        }
    }
    const QString requested = choice.requested.isEmpty() ? QStringLiteral("<none>") : choice.requested;
    qInfo().noquote() << "widget style requested=" + requested
                            + " chosen=" + qApp->style()->objectName();
}
#endif

int main(int argc, char **argv)
{
    QCoreApplication::setApplicationName(QStringLiteral("speecher"));
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.firemonster612.speecher"));
    QCoreApplication::setOrganizationName(QString::fromLatin1(SettingsKeys::Organization));
    const QString logPath = installLogHandler();
    migrateSettings();
    const std::shared_ptr<const PlatformComposition> platform = platformComposition();

    const QStringList arguments = commandLineArguments(argc, argv);
    // Before parsing, which prints --help, --version and usage errors itself.
    // A plain launch has no command line to talk to.
    if (argc > 1) {
        attachParentConsole();
    }
    const CommandLineDecision decision = parseCommandLine(arguments, logPath);
    if (decision.mode == LaunchMode::Exit) {
        return decision.exitCode;
    }
    if (decision.mode == LaunchMode::RunCli) {
        // No QApplication: talking to a running instance must not need a display.
        QCoreApplication app(argc, argv);
        return runCliCommand(decision, platform);
    }
    if (decision.mode == LaunchMode::TranscribeHeadless) {
        // Its own settings reader, secrets and providers: no IPC, no display,
        // and nothing shared with a running instance's dictation.
        QCoreApplication app(argc, argv);
        SettingsStore settings;
        LocalModelStore localModels;
        ProviderRegistry providers;
        registerProviders(providers, settings.secrets(), &localModels);
        if (decision.transcribeFiles == QStringList{kStdinFile}) {
            if (stdinIsTerminal()) {
                std::cerr << "transcribe - reads audio piped to stdin, not a terminal\n";
                return 2;
            }
#ifdef Q_OS_WIN
            // Audio piped to `transcribe -` is binary.
            _setmode(_fileno(stdin), _O_BINARY);
#endif
            installStdinSpoolRemoval();
        }
        return runHeadlessTranscribe(decision.transcribeFiles, decision.headless, &settings, &providers,
                                     std::cin, std::cout, std::cerr, stderrIsTerminal());
    }
    if (decision.mode == LaunchMode::ListenHeadless) {
        // As transcribe: its own microphone and providers, beside whatever a
        // running instance is doing.
        QCoreApplication app(argc, argv);
        SettingsStore settings;
        LocalModelStore localModels;
        ProviderRegistry providers;
        registerProviders(providers, settings.secrets(), &localModels);
        AudioInput *microphone =
            microphoneAccessGranted(*platform, &app) ? platform->createAudioInput(&settings, &app) : nullptr;
        const bool enterStops = installListenStopHandlers();
        return runHeadlessListen(decision.headless, decision.untilSilenceMs, microphone,
                                 [] { return g_listenStopRequested.load(); }, enterStops, &settings, &providers,
                                 std::cout, std::cerr);
    }
    if (decision.mode == LaunchMode::ListProviders) {
        QCoreApplication app(argc, argv);
        return runProvidersCommand(decision.json, platform, std::cout, std::cerr);
    }

#ifdef SPEECHER_WITH_WINUI
    auto winUiHost = std::make_unique<WinUiHost>();
#endif
    QApplication app(argc, argv);
#ifdef SPEECHER_WITH_WINUI
    winUiHost->installNativeEventFilter();
#endif
    // A second store, because the theme has to be applied before the first
    // widget exists and the controller's store is not built yet.
    SettingsStore startupSettings;
#ifdef Q_OS_LINUX
    if (!qEnvironmentVariableIsEmpty("APPIMAGE")) {
        applyHostWidgetStyle(startupSettings.theme());
    }
#endif
#ifdef Q_OS_LINUX
    AppImageUpdater::waitForRestartParent();
    if (!qEnvironmentVariableIsEmpty("APPIMAGE")) {
        QIcon::setFallbackThemeName(QStringLiteral("breeze"));
    }
#endif
#ifndef SPEECHER_WITH_WINUI
    Theme::apply(startupSettings.theme());
#endif

    const bool daemon = decision.mode == LaunchMode::RunDaemon;
    app.setQuitOnLastWindowClosed(quitOnLastWindowClosed(decision.mode));

    ApplicationController controller(daemon, platform);
    QObject::connect(&controller,
                     &ApplicationController::quitRequested,
                     &app,
                     &QCoreApplication::quit,
                     Qt::QueuedConnection);
    // The one place a platform's front end is chosen; see
    // docs/adr/0001-per-platform-front-ends.md.
#ifdef SPEECHER_WITH_WINUI
    WinFrontEnd frontEnd(&controller, std::move(winUiHost));
#elif defined(SPEECHER_WITH_SWIFT_UI)
    MacFrontEnd frontEnd(&controller);
#else
    QtFrontEnd frontEnd(&controller);
#endif
    controller.setFrontEnd(&frontEnd);
#ifdef SPEECHER_WITH_WINUI
    if (arguments.contains(QStringLiteral("--winui-spike"))) {
        QTimer::singleShot(0, &controller, [&frontEnd] {
            frontEnd.showDictationError(QStringLiteral("WinUI hosting spike"));
        });
    }
#endif
    controller.updates()->start();
    QString ipcError;
    if (!controller.startIpc(&ipcError)) {
        if (!decision.grabPath.isEmpty()) {
            if (ipcError.startsWith(QStringLiteral("Another Speecher instance"))) {
                std::cerr << "--grab cannot be used while another Speecher instance is running\n";
            } else {
                std::cerr << ipcError.toStdString() << "\n";
            }
            return 1;
        }
        const QString showCommand = !decision.transcribeFiles.isEmpty()
            ? QStringLiteral("transcribe")
            : decision.showSettings ? QStringLiteral("showSettings")
                                    : QStringLiteral("showMain");
        // An older running instance answers a command it does not know, such
        // as transcribe, with a refusal; say so rather than exit as if the
        // files had opened.
        IpcResponse response;
        const auto answered = [&response, &showCommand] {
            if (response.ok) {
                return 0;
            }
            std::cerr << "The running Speecher instance refused " << showCommand.toStdString() << ": "
                      << response.message.toStdString() << "\n";
            return 1;
        };
#ifdef Q_OS_WIN
        if (!daemon) {
            AllowSetForegroundWindow(ASFW_ANY);
            auto result = SingleInstanceIpc::sendCommandDetailed(
                showCommand, SessionOverrides(), decision.transcribeFiles, &response);
            // The startup claim can precede the winning instance's pipe listener.
            if (ipcError.startsWith(QStringLiteral("Another Speecher instance"))) {
                QDeadlineTimer deadline(750);
                while (result == IpcCommandResult::Unavailable && !deadline.hasExpired()) {
                    QThread::msleep(25);
                    const auto remaining = deadline.remainingTime();
                    if (remaining <= 0) {
                        break;
                    }
                    result = SingleInstanceIpc::sendCommandDetailed(
                        showCommand, SessionOverrides(), decision.transcribeFiles, &response, int(remaining));
                }
            }
            if (result == IpcCommandResult::Sent) {
                return answered();
            }
        }
#else
        if (!daemon
            && SingleInstanceIpc::sendCommandDetailed(showCommand, SessionOverrides(), decision.transcribeFiles, &response)
                == IpcCommandResult::Sent) {
            return answered();
        }
#endif
        std::cerr << ipcError.toStdString() << "\n";
        return 1;
    }

    if ((!controller.settings()->setupCompleted() && decision.grabPath.isEmpty()) || decision.showSetup) {
        QTimer::singleShot(0, &controller, [&controller, &decision] {
            controller.showSetupAssistant();
            // Held by the controller until setup completes, then opened.
            if (!decision.transcribeFiles.isEmpty()) {
                controller.showTranscribeFiles(decision.transcribeFiles);
            }
        });
    } else {
        if (decision.startListening) {
            QTimer::singleShot(0, &controller, [&controller, &decision] {
                const SessionOverrides &overrides = decision.sessionOverrides;
                if (overrides.outputFormat || overrides.writingProfile || overrides.spokenLanguage) {
                    controller.handleIpcCommand(QStringLiteral("start"),
                                                overrides.outputFormat ? outputFormatName(*overrides.outputFormat)
                                                                       : QString(),
                                                nullptr,
                                                {},
                                                overrides.writingProfile.value_or(QString()),
                                                overrides.spokenLanguage.value_or(QString()));
                } else {
                    controller.startListening();
                }
            });
        }
        if (decision.showSettings) {
            QTimer::singleShot(0, &controller, &ApplicationController::showSettings);
        }
        // An update restart records which windows were on screen; the relaunched
        // process puts them back. Never the microphone: a new process has no
        // dictation gesture behind it, so nothing here starts listening.
        // Read-and-clear so a normal launch never replays it, and only honor a
        // token young enough to be from an actual relaunch.
        const QString restore = controller.settings()->updatesRestoreState();
        const qint64 restoreAge = QDateTime::currentMSecsSinceEpoch()
            - controller.settings()->updatesRestoreStateTime();
        controller.settings()->setUpdatesRestoreState({});
        if (!restore.isEmpty() && restoreAge >= 0 && restoreAge < 120000
            && restore.contains(QStringLiteral("settings"))) {
            QTimer::singleShot(0, &controller, &ApplicationController::showMain);
        }
        if (!decision.grabPath.isEmpty()) {
            controller.showMainWindow();
        } else if (!daemon && decision.transcribeFiles.isEmpty()) {
            // A launch that opens files brings up the Transcribe window alone.
            // macOS hands Finder's files over as events once the loop runs,
            // so let those arrive before deciding.
            // Files arriving later still replace it; see showDefaultMainWindow.
            QTimer::singleShot(0, &controller, [&controller] {
                QCoreApplication::processEvents();
                if (!controller.filesOpened()) {
                    controller.showDefaultMainWindow();
                }
            });
        }
        if (!decision.transcribeFiles.isEmpty()) {
            QTimer::singleShot(0, &controller, [&controller, &decision] {
                controller.showTranscribeFiles(decision.transcribeFiles);
            });
        }
    }
    // A grab ends by itself, so it keeps the console and the launcher waits
    // for its exit status.
    if (!decision.grabPath.isEmpty()) {
        QTimer::singleShot(600, &controller, [&controller, &app, &decision] {
            app.exit(controller.grabMainWindow(decision.grabPath) ? 0 : 1);
        });
    } else {
        detachParentConsole();
    }
    return app.exec();
}
