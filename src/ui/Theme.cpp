#include "ui/Theme.h"

#include <QApplication>
#include <QStyleHints>

namespace speecher::Theme {

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
}

} // namespace speecher::Theme
