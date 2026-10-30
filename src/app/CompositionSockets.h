#pragma once

#include <QString>

namespace speecher {

// The local-socket names single-instance IPC listens on and dials. Every
// composition derives them here rather than spelling out the scheme again: a
// listener and a caller that disagree would start a second app instead of
// reaching the running one.

// "speecher-<user>", plus "-<suffix>" when one is given; an isolated
// instance's name stands in for "speecher". Linux puts it in the user's
// runtime directory; elsewhere it is a bare name, which macOS keeps in the
// user's own temporary folder and Windows makes a pipe.
QString appSocketName(const QString &suffix = {});

// Distinguishes builds installed at different paths for the same user.
QString executablePathSocketName();

} // namespace speecher
