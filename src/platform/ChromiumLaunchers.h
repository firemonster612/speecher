#pragma once

#include <QString>
#include <QStringList>

namespace speecher {

struct DesktopIntegrationRemoval;

// Per-user copies of the desktop entries of installed Chromium and Electron
// apps that start them with --force-renderer-accessibility. On Linux these
// apps build the accessibility tree that holds a page's text only when
// started with that flag: nothing on AT-SPI turns it on, live or at launch.

// Whether an Exec line starts a Chromium or Electron app: a Flatpak with a
// known app id, or a program, the one after env and its options and variables
// where the line starts with env, that resolves, through PATH and symlinks, into
// a directory holding Chromium's chrome_100_percent.pak or resources.pak, or
// an Electron app's resources/app.asar, or a distribution's chromium wrapper.
bool launchesChromium(const QString &execLine);

// The Exec line with --force-renderer-accessibility before the first field
// code (%U, or a Flatpak's @@u %U @@) or -- after the program, or at its end
// without either. For a Flatpak that is after its app id, so the flag reaches
// the app rather than flatpak. A line that has the flag comes back unchanged.
QString withAccessibilityFlag(const QString &execLine);

// For each Chromium or Electron app with a desktop entry in the applications
// directory of dataDirs after the first, writes a copy into the first's, the
// user's, with withAccessibilityFlag() on each Exec line that starts the app
// and a mark that it is Speecher's. A desktop entry or symlink already in the
// user's directory stays, unless it is a copy of Speecher's that no one
// changed: that one is written again, and deleted when its entry is in none of
// the other dataDirs. The first of the other dataDirs holding an entry is the
// one copied, as the desktop reads them. The apps pick the flag up once
// started again from their launcher.
bool writeAccessibleLaunchers(const QStringList &dataDirs, QString *error = nullptr);

// Whether dataHome's applications directory holds a copy of Speecher's that
// no one changed, which removeAccessibleLaunchers() would delete.
bool hasAccessibleLaunchers(const QString &dataHome);

// Deletes the copies writeAccessibleLaunchers() wrote into dataHome's
// applications directory, unless someone has changed them since, and records
// the outcome in removal as the "Chromium app launchers".
void removeAccessibleLaunchers(const QString &dataHome, DesktopIntegrationRemoval &removal);

} // namespace speecher
