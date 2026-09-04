#pragma once

#include <QString>
#include <QStringList>

namespace speecher {

struct LinuxStyleChoice
{
    QString requested;
    QString chosen;
    QString fallback;
};

QString adwaitaStyleName(const QString &applicationTheme, bool desktopPrefersDark);

LinuxStyleChoice chooseLinuxStyle(const QString &overrideStyle,
                                  const QString &kdeWidgetStyle,
                                  const QString &currentDesktop,
                                  const QString &platformTheme,
                                  const QString &currentStyle,
                                  const QStringList &availableStyles,
                                  const QString &applicationTheme,
                                  bool desktopPrefersDark);

}
