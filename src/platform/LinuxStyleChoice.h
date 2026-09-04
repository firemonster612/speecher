#pragma once

#include <QString>
#include <QStringList>

namespace speecher {

struct LinuxStyleChoice
{
    QString requested;
    QString chosen;
};

LinuxStyleChoice chooseLinuxStyle(const QString &overrideStyle,
                                  const QString &kdeWidgetStyle,
                                  const QString &currentDesktop,
                                  const QStringList &availableStyles,
                                  bool prefersDark);

}
