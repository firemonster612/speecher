#pragma once

#include <QString>

namespace speecher {

// On macOS the speecher binary lives inside the app bundle, off the PATH.
// The menu bar panel's "Install command line tool…" puts a speecher command
// here, in a folder on the default PATH (/etc/paths).
inline constexpr auto kCommandLineToolPath = "/usr/local/bin/speecher";

// The sh command that writes toolPath as a two-line script exec'ing
// binaryPath with the caller's arguments, replacing whatever was there, and
// creates its folder if it is missing. A script rather than a symlink, so the
// binary runs from inside its bundle and finds its frameworks and plugins.
QString commandLineToolInstallCommand(const QString &binaryPath, const QString &toolPath);

} // namespace speecher
