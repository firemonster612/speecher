#include "platform/LinuxStyleChoice.h"

namespace speecher {

namespace {

QString availableStyle(const QString &requested, const QStringList &availableStyles)
{
    for (const QString &available : availableStyles) {
        if (available.compare(requested, Qt::CaseInsensitive) == 0) {
            return available;
        }
    }
    return {};
}

bool isGtkDesktop(const QString &desktop)
{
    static const QStringList gtkDesktops = {
        QStringLiteral("GNOME"),
        QStringLiteral("X-Cinnamon"),
        QStringLiteral("PANTHEON"),
        QStringLiteral("UNITY"),
        QStringLiteral("MATE"),
        QStringLiteral("XFCE"),
        QStringLiteral("LXDE"),
    };
    const QStringList desktops = desktop.split(QLatin1Char(':'), Qt::SkipEmptyParts);
    for (const QString &part : desktops) {
        if (gtkDesktops.contains(part, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

bool usesGtkPlatformTheme(const QString &platformTheme, const QString &desktop)
{
    if (!platformTheme.isEmpty()) {
        return platformTheme.contains(QStringLiteral("gtk"), Qt::CaseInsensitive);
    }
    return isGtkDesktop(desktop);
}

bool prefersDark(const QString &applicationTheme, bool desktopPrefersDark)
{
    if (applicationTheme == QStringLiteral("light")) {
        return false;
    }
    if (applicationTheme == QStringLiteral("dark")) {
        return true;
    }
    return desktopPrefersDark;
}

}

LinuxStyleChoice chooseLinuxStyle(const QString &overrideStyle,
                                  const QString &kdeWidgetStyle,
                                  const QString &currentDesktop,
                                  const QString &platformTheme,
                                  const QString &currentStyle,
                                  const QStringList &availableStyles,
                                  const QString &applicationTheme,
                                  bool desktopPrefersDark)
{
    const bool kde = currentDesktop.contains(QStringLiteral("KDE"), Qt::CaseInsensitive);
    QString requested = overrideStyle;
    if (requested.isEmpty()) {
        if (kde) {
            requested = kdeWidgetStyle;
        } else if (usesGtkPlatformTheme(platformTheme, currentDesktop)) {
            requested = prefersDark(applicationTheme, desktopPrefersDark)
                ? QStringLiteral("adwaita-dark")
                : QStringLiteral("adwaita");
        } else {
            return {{}, currentStyle, currentStyle};
        }
    }

    const QString fallbackName = kde ? QStringLiteral("Breeze") : QStringLiteral("Fusion");
    const QString availableFallback = availableStyle(fallbackName, availableStyles);
    const QString fallback = availableFallback.isEmpty() ? currentStyle : availableFallback;
    const QString requestedStyle = availableStyle(requested, availableStyles);
    if (!requestedStyle.isEmpty()) {
        return {requested, requestedStyle, fallback};
    }

    return {requested, fallback, fallback};
}

}
