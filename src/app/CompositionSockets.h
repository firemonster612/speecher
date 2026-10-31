#pragma once

#include <QString>

class QLocalSocket;

namespace speecher {

// Where Speecher's local sockets live and who may be at their other end. Every
// composition derives the single-instance names here rather than spelling out
// the scheme again: a listener and a caller that disagree would start a second
// app instead of reaching the running one.

// "speecher-<user>", plus "-<suffix>" when one is given; an isolated
// instance's name stands in for "speecher".
QString appSocketName(const QString &suffix = {});

// Distinguishes builds installed at different paths for the same user.
QString executablePathSocketName();

#ifdef Q_OS_WIN
// The name earlier releases listened on, derived from the user name alone, so
// a command still reaches a Speecher one of them started.
QString legacyAppSocketName();
#endif

#ifdef Q_OS_UNIX
// name inside $XDG_RUNTIME_DIR, or without it systemd's /run/user/<uid>,
// which a shell lacking the variable (cron, su) shares with the desktop
// session, or else Qt's fallback, each only while it is this user's alone.
// Empty when there is no such folder: a bare name would put the socket in the
// shared /tmp, where another user can take it first.
QString inRuntimeDirectory(const QString &name);
#endif

// Whether the process at the other end of a connected socket runs as this
// user. A socket in a shared folder (/tmp, where Speecher listened before,
// or a macOS TMPDIR set to it) can be another user's. On Windows the pipe's
// permissions keep other users out.
bool peerIsThisUser(const QLocalSocket &socket);

} // namespace speecher
