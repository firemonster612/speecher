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
    // The settings page to open, from `speecher settings <page>` or
    // --settings-page; empty for whichever page is current.
    QString settingsPage;
    bool showSetup = false;
    QString grabPath;
};

// The IPC command that opens settings, at `page` when one is named.
QString showSettingsCommand(const QString &page);

// Decides what the process is for, before any GUI type is constructed, so that
// `speecher status` never opens a display. Prints the --version banner and any
// rejected option itself, because both are the whole of what those runs do.
CommandLineDecision parseCommandLine(const QStringList &arguments, const QString &logPath);

// Linux and macOS keep handling shortcuts and IPC after their last window closes.
bool quitOnLastWindowClosed(LaunchMode mode);

// Sends the decision's command to a running instance, or starts one detached
// when there is none. Needs a running QCoreApplication for the socket.
int runCliCommand(const CommandLineDecision &decision,
                  const std::shared_ptr<const SingleInstancePlatform> &platform);

} // namespace speecher
