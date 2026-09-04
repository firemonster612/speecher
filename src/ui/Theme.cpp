#include "ui/Theme.h"

#include <QApplication>
#include <QStyleHints>

#ifdef Q_OS_MACOS
#include "platform/mac/MacWindowChrome.h"
#endif

namespace speecher::Theme {

namespace {
bool s_overrideHonored = true;
}

void apply(const QString &theme)
{
    if (!qApp) {
        return;
    }
    Qt::ColorScheme scheme = Qt::ColorScheme::Unknown;
    if (theme == QStringLiteral("light")) {
        scheme = Qt::ColorScheme::Light;
    } else if (theme == QStringLiteral("dark")) {
        scheme = Qt::ColorScheme::Dark;
    }
    qApp->styleHints()->setColorScheme(scheme);
    // Some platform themes ignore the request. Only an explicit scheme can
    // tell; "system" always matches whatever the desktop reports.
    if (scheme != Qt::ColorScheme::Unknown) {
        s_overrideHonored = qApp->styleHints()->colorScheme() == scheme;
    }
#ifdef Q_OS_MACOS
    // Qt only repaints its own widgets; the traffic lights and the vibrancy
    // panels follow NSApp.appearance.
    mac::applyAppearance(scheme);
#endif
}

bool overrideHonored()
{
    return s_overrideHonored;
}

} // namespace speecher::Theme
