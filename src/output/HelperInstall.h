#pragma once

#include <QString>
#include <QStringList>

#include <functional>

namespace speecher::helpers {

enum class HelperAction {
    Install,
    Remove,
};

QString currentUserName();
// The passwd database's answer, which applies from the next sign-in.
bool userInGroup(const char *group, const QString &userName);
// This process's supplementary groups, which is what the kernel checks now.
bool currentSessionInGroup(const char *group);
bool runProgram(const QString &program,
                const QStringList &arguments,
                QString *error,
                int timeoutMs = 60000);
// In a session with no polkit agent (a bare compositor, an app launched from
// a systemd user unit), pkexec falls back to a textual agent on the
// controlling terminal, and a GUI process has none, so setup dies with
// "Error opening current controlling terminal". The helpers therefore run
// pkexec on a private PTY and hand the agent's questions to this callback:
// promptText is the conversation since the last answered prompt, echoOff is
// true when the answer is hidden (a password). Return false to cancel. Called on whichever thread
// runs the helper. Sessions with a real agent authenticate through it as
// before and never prompt here.
using PkexecPrompt =
    std::function<bool(const QString &promptText, bool echoOff, QString *reply)>;
void setPkexecPrompt(PkexecPrompt prompt);
// Runs a program the way the setup helpers run pkexec: on a private PTY whose
// authentication conversation is answered through the PkexecPrompt. Exposed
// so tests can drive the conversation with a fake pkexec.
bool runPkexecConversation(const QString &program,
                           const QStringList &arguments,
                           QString *error,
                           int timeoutMs);
// Runs the setup helper as root through pkexec with a fixed argv: the helper,
// --install or --remove, --user and the current user. Root never executes
// from a user-writable path: a helper not already at its root-owned installed
// path (an AppImage mount, a build directory) is first copied, with its
// companion files, to a root-owned directory by pkexec'd /usr/bin/install,
// and that copy is what runs. The intermediate hop onto a normal filesystem
// exists because pkexec's root cannot read the AppImage's private FUSE mount.
bool runSetupHelper(const char *installedHelperPath,
                    const QStringList &companionFileNames,
                    HelperAction action,
                    QString *error);

} // namespace speecher::helpers
