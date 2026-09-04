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
        QStringLiteral("GTK"),
        QStringLiteral("Unity"),
        QStringLiteral("Cinnamon"),
        QStringLiteral("X-Cinnamon"),
        QStringLiteral("MATE"),
        QStringLiteral("XFCE"),
        QStringLiteral("Budgie"),
    };
    const QStringList desktops = desktop.split(QLatin1Char(':'), Qt::SkipEmptyParts);
    for (const QString &part : desktops) {
        if (gtkDesktops.contains(part, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

}

LinuxStyleChoice chooseLinuxStyle(const QString &overrideStyle,
                                  const QString &kdeWidgetStyle,
                                  const QString &currentDesktop,
                                  const QStringList &availableStyles,
                                  bool prefersDark)
{
    const bool kde = currentDesktop.contains(QStringLiteral("KDE"), Qt::CaseInsensitive);
    QString requested = overrideStyle;
    if (requested.isEmpty() && kde) {
        requested = kdeWidgetStyle;
    } else if (requested.isEmpty() && isGtkDesktop(currentDesktop)) {
        requested = prefersDark ? QStringLiteral("adwaita-dark") : QStringLiteral("adwaita");
    } else if (requested.isEmpty()) {
        requested = QStringLiteral("Fusion");
    }

    const QString requestedStyle = availableStyle(requested, availableStyles);
    if (!requestedStyle.isEmpty()) {
        return {requested, requestedStyle};
    }

    const QString fallback = kde ? QStringLiteral("Breeze") : QStringLiteral("Fusion");
    const QString availableFallback = availableStyle(fallback, availableStyles);
    return {requested, availableFallback.isEmpty() ? fallback : availableFallback};
}

}
