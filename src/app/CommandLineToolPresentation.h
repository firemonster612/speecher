#pragma once

#include <QString>

namespace speecher {

// How the macOS menu bar panel words installing the speecher command
// (CommandLineTool.h): the button, then what it says once the command is
// installed or could not be. A cancelled password prompt says nothing.
QString installCommandLineToolCaption();
QString commandLineToolInstalledTitle();
QString commandLineToolInstalledText();
QString commandLineToolFailedTitle();
// Why it was not installed when the app is not in a lasting location
// (isLastingAppLocation).
QString commandLineToolMoveAppText();

} // namespace speecher
