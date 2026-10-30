#pragma once

#include "app/HeadlessTranscribe.h"
#include "app/IpcMessages.h"
#include "dictation/DictationTypes.h"

#include <QString>
#include <QStringList>

#include <optional>

namespace speecher {

enum class LaunchMode {
    // Nothing left to do; the exit code says how it went.
    Exit,
    // One command against a running instance, then quit. Needs no display.
    RunCli,
    // A window-less process that answers the shortcut and the IPC socket.
    RunDaemon,
    RunGui,
    // `speecher transcribe` with options: transcribe the files in this
    // process without a window, print the results and quit.
    TranscribeHeadless,
    // `speecher listen`: record from the microphone in this process, print
    // the transcript and quit.
    ListenHeadless,
    // `speecher providers`: print every provider's state as the running app
    // or, without one, this process knows it, and quit.
    ListProviders,
};

struct CommandLineDecision {
    LaunchMode mode = LaunchMode::RunGui;
    int exitCode = 0;
    // RunCli: the IPC command to send.
    IpcCommand ipcCommand = IpcCommand::Status;
    // RunCli's addVocabulary: the terms to save. record start: --vocab-file's
    // terms, for that recording only.
    QStringList vocabularyTerms;
    // --format and --profile, for toggle, start and a daemon's --start-listening.
    SessionOverrides sessionOverrides;
    bool startListening = false;
    bool showSettings = false;
    bool showSetup = false;
    // RunGui: absolute paths to open in the Transcribe window, from
    // `speecher transcribe <files...>` or bare audio file arguments (what a
    // file manager's "Open with" passes). TranscribeHeadless: the files to
    // transcribe.
    QStringList transcribeFiles;
    // TranscribeHeadless and ListenHeadless: the choices for the run.
    HeadlessTranscribeOptions headless;
    // ListenHeadless: stop after this long without speech once there was some.
    std::optional<int> untilSilenceMs;
    // ListProviders: a JSON array instead of a table. RunCli's record status
    // and status: one JSON object, and status --watch one a line.
    bool json = false;
    // RunCli's record start: the absolute path of --to, or empty for the
    // default file, and --mic-only.
    QString recordPath;
    bool recordMicrophoneOnly = false;
};

// Decides what the process is for, before any GUI type is constructed, so that
// `speecher status` never opens a display. Prints the --version banner, the
// --help text and any rejected option itself, because each is the whole of
// what those runs do.
CommandLineDecision parseCommandLine(const QStringList &arguments, const QString &logPath);

// The arguments an update relaunches the app with, given the ones it was
// started with: only whether it ran as a daemon. Everything else made that
// launch *do* something (start dictation, open a window, transcribe files),
// which a relaunch without a fresh gesture must not repeat.
QStringList updateRelaunchArguments(const QStringList &arguments);

// Linux and macOS keep handling shortcuts and IPC after their last window closes.
bool quitOnLastWindowClosed(LaunchMode mode);

// Whether this launch opens the setup assistant by itself: when asked to, or
// when setup is pending, except for --daemon, which shows no window until a
// command (start, toggle, the tray) needs setup and opens it then.
bool opensSetupAtLaunch(const CommandLineDecision &decision, bool setupPending);

} // namespace speecher
