#pragma once

#include "core/OutputFormat.h"

#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

namespace speecher {

class SingleInstancePlatform;

enum class LaunchMode {
    // Nothing left to do; the exit code says how it went.
    Exit,
    // One command against a running instance, then quit. Needs no display.
    RunCli,
    // A window-less process that answers the shortcut and the IPC socket.
    RunDaemon,
    RunGui,
};

struct CommandLineDecision {
    LaunchMode mode = LaunchMode::RunGui;
    int exitCode = 0;
    // RunCli: the IPC command to send.
    QString ipcCommand;
    std::optional<OutputFormat> outputFormat;
    bool startListening = false;
    bool showSettings = false;
    bool showSetup = false;
    QString grabPath;
};

// Decides what the process is for, before any GUI type is constructed, so that
// `speecher status` never opens a display. Prints the --version banner and any
// rejected option itself, because both are the whole of what those runs do.
CommandLineDecision parseCommandLine(const QStringList &arguments, const QString &logPath);

// Drops the flags that make a launch *do* something — start dictation, open
// settings, open setup — and keeps everything else. An update relaunches the
// app with the argv it was started with, and resuming a recording or reopening
// a window without a fresh gesture is not what the user asked for.
QStringList argumentsWithoutStartupActions(const QStringList &arguments);

// Linux and macOS keep handling shortcuts and IPC after their last window closes.
bool quitOnLastWindowClosed(LaunchMode mode);

// Sends the decision's command to a running instance, or starts one detached
// when there is none. Needs a running QCoreApplication for the socket.
int runCliCommand(const CommandLineDecision &decision,
                  const std::shared_ptr<const SingleInstancePlatform> &platform);

} // namespace speecher
