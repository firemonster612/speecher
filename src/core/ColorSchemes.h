#pragma once

#include <QColor>
#include <QList>
#include <QString>
#include <QStringList>

namespace speecher {

// Reads the desktop's colour schemes: KDE's .colors files and kdeglobals,
// which holds the active scheme's colours. The UI maps a scheme's roles onto
// its own palette; this only reads them.
struct ColorScheme {
    // The file's base name, as KDE names a scheme ("BreezeDark").
    QString id;
    // The name the scheme gives itself, or the id when it gives none.
    QString name;
    // The window's background and its text.
    QColor window;
    QColor text;
    // The selection colour, or the accent the desktop chose instead.
    QColor accent;
    QColor negative;

    // A scheme the UI can use has at least a window and its text.
    bool isValid() const { return window.isValid() && text.isValid(); }
};

// The scheme in a .colors file or kdeglobals; invalid when the file is
// missing or has no window colours.
ColorScheme readKdeColorScheme(const QString &path);

// The .colors files in the color-schemes folder of each data dir, sorted by
// name. dataDirs is in XDG order, so a user's copy hides a system one of the
// same id.
QList<ColorScheme> installedColorSchemes(const QStringList &dataDirs);

// Where installedColorSchemes looks in each data dir.
QString colorSchemesFolder(const QString &dataDir);

} // namespace speecher
