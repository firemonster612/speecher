#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace speecher::WaylandClipboardProcess {

QString wlCopyExecutable();
QString wlPasteExecutable();
QString helperExecutable();

// A wl-clipboard command to run. Its standard output is read only when
// collectsOutput is set: wl-copy leaves a background copy of itself holding
// whatever it was given.
struct Command {
    QString executable;
    // The name failures are reported under, such as "wl-paste".
    QString tool;
    QStringList arguments;
    // Written to standard input, which is then closed; none leaves it closed.
    std::optional<QByteArray> input;
    bool collectsOutput = false;
    int timeoutMs = 5000;
    qsizetype maximumOutputBytes = 16 * 1024 * 1024;
};

struct CommandResult {
    // Exited 0 within the time allowed.
    bool ok = false;
    QByteArray output;
    QString error;
};

// Starts command and returns at once. done runs once, when the command has
// exited, failed or run out of time, unless context is gone by then; a command
// that runs out of time or gives too much output is killed.
void run(const Command &command, QObject *context, std::function<void(const CommandResult &)> done);
bool looksLikeEmptyClipboardError(const QString &message);

} // namespace speecher::WaylandClipboardProcess
