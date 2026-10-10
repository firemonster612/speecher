#pragma once

#include <QString>

namespace speecher {

// How the macOS menu bar panel words installing the speecher command
// (CommandLineTool.h): the button, then what it says once the command is
// installed or could not be. A cancelled password prompt says nothing.
QString installCommandLineToolCaption();
// What the administrator password prompt says it is for.
QString commandLineToolPasswordPrompt();
QString commandLineToolInstalledTitle();
QString commandLineToolInstalledText();
QString commandLineToolFailedTitle();
// Why it was not installed when the app runs from a read-only volume, which
// goes away: an ejected disk image, or Gatekeeper's translocated copy.
QString commandLineToolMoveAppText();

} // namespace speecher
