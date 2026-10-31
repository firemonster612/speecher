#pragma once

// Runs a helper command, such as wl-paste or ydotool, in the background and
// answers once it has exited, failed or run out of time, so delivery never
// waits on one on the UI thread.

#include <QByteArray>
#include <QObject>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace speecher {

// Its standard output is read only when collectsOutput is set: wl-copy leaves
// a background copy of itself holding whatever it was given.
struct ProcessCommand {
    QString executable;
    // The name failures are reported under, such as "wl-paste".
    QString tool;
    QStringList arguments;
    // Written to standard input, which is then closed; none leaves it closed.
    std::optional<QByteArray> input;
    bool collectsOutput = false;
    // Covers starting the command as well as running it.
    int timeoutMs = 5000;
    qsizetype maximumOutputBytes = 16 * 1024 * 1024;
    // Speecher's own environment when none is given.
    std::optional<QProcessEnvironment> environment;
};

struct ProcessResult {
    // Exited 0 within the time allowed.
    bool ok = false;
    QByteArray output;
    QString error;
};

// Starts command and returns at once. done runs once, when the command has
// exited, failed or run out of time, unless context is gone by then; a command
// that runs out of time or gives too much output is killed.
void runProcess(const ProcessCommand &command, QObject *context, std::function<void(const ProcessResult &)> done);

} // namespace speecher
