#include "app/CommandLineToolPresentation.h"

namespace speecher {

QString installCommandLineToolCaption()
{
    return QStringLiteral("Install command line tool…");
}

QString commandLineToolPasswordPrompt()
{
    return QStringLiteral("Speecher is installing the speecher command.");
}

QString commandLineToolInstalledTitle()
{
    return QStringLiteral("Installed the speecher command");
}

QString commandLineToolInstalledText()
{
    return QStringLiteral("Run speecher from Terminal. If you move Speecher, install the command again.");
}

QString commandLineToolFailedTitle()
{
    return QStringLiteral("Could not install the speecher command");
}

QString commandLineToolMoveAppText()
{
    return QStringLiteral("Speecher is running from a disk image or another read-only location. Move it to the "
                          "Applications folder, open it again, then install the command.");
}

} // namespace speecher
