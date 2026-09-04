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
    if (platformTheme.contains(QStringLiteral("gtk"), Qt::CaseInsensitive)) {
        return true;
    }
    if (!platformTheme.isEmpty()
        && !platformTheme.contains(QStringLiteral("portal"), Qt::CaseInsensitive)
        && platformTheme.compare(QStringLiteral("gnome"), Qt::CaseInsensitive) != 0) {
        return false;
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
    const bool gtk = usesGtkPlatformTheme(platformTheme, currentDesktop);
    QString requested = overrideStyle;
    if (requested.isEmpty()) {
        if (kde) {
            requested = kdeWidgetStyle;
        } else if (gtk) {
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
    QString requestedName = requested;
    if (requested.compare(QStringLiteral("Adwaita"), Qt::CaseInsensitive) == 0
        || requested.compare(QStringLiteral("Adwaita-Dark"), Qt::CaseInsensitive) == 0) {
        requestedName = prefersDark(applicationTheme, desktopPrefersDark)
            ? QStringLiteral("adwaita-dark")
            : QStringLiteral("adwaita");
    }
    const QString requestedStyle = availableStyle(requestedName, availableStyles);
    if (!requestedStyle.isEmpty()) {
        return {requested, requestedStyle, fallback};
    }
    if (!kde && !gtk) {
        return {requested, currentStyle, currentStyle};
    }
    if (gtk) {
        const QString adwaita = prefersDark(applicationTheme, desktopPrefersDark)
            ? QStringLiteral("adwaita-dark")
            : QStringLiteral("adwaita");
        const QString availableAdwaita = availableStyle(adwaita, availableStyles);
        if (!availableAdwaita.isEmpty()) {
            return {requested, availableAdwaita, fallback};
        }
    }

    return {requested, fallback, fallback};
}

}
