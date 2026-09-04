#include "ui/Theme.h"

#include <QApplication>
#include <QStyle>
#include <QStyleHints>

#ifdef Q_OS_LINUX
#include "platform/LinuxStyleChoice.h"
#endif

#ifdef Q_OS_MACOS
#include "platform/mac/MacWindowChrome.h"
#endif

namespace speecher::Theme {

namespace {
bool s_overrideHonored = true;

#ifdef Q_OS_LINUX
void applyAdwaitaVariant(const QString &theme)
{
    if (qEnvironmentVariableIsEmpty("APPIMAGE")) {
        return;
    }
    const QString current = qApp->style()->objectName();
    const QString light = adwaitaStyleName(QStringLiteral("light"), false);
    const QString dark = adwaitaStyleName(QStringLiteral("dark"), false);
    if (current.compare(light, Qt::CaseInsensitive) != 0
        && current.compare(dark, Qt::CaseInsensitive) != 0) {
        return;
    }
    const QString requested = adwaitaStyleName(
        theme,
        qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark);
    if (current.compare(requested, Qt::CaseInsensitive) != 0
        && !QApplication::setStyle(requested)) {
        qWarning().noquote() << "Could not load widget style " + requested;
    }
}
#endif
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
#ifdef Q_OS_LINUX
    applyAdwaitaVariant(theme);
#endif
#ifdef Q_OS_MACOS
    // Qt only repaints its own widgets; the traffic lights and the vibrancy
    // panels follow NSApp.appearance.
    mac::applyAppearance(scheme);
#endif
}

QString normalizedSetting(const QString &theme, bool overrideHonored)
{
    return overrideHonored ? theme : QStringLiteral("system");
}

bool overrideHonored()
{
    return s_overrideHonored;
}

} // namespace speecher::Theme
