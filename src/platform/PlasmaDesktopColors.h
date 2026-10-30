#pragma once

#include "platform/DesktopColors.h"

#include <QFileSystemWatcher>
#include <QStringList>

namespace speecher {

// Plasma's colour scheme, as the kdeglobals in effect holds it.
class PlasmaDesktopColors final : public DesktopColors {
    Q_OBJECT

public:
    explicit PlasmaDesktopColors(QObject *parent = nullptr);

    ColorScheme scheme() const override { return m_scheme; }

private:
    void read();

    QFileSystemWatcher m_watcher;
    ColorScheme m_scheme;
};

} // namespace speecher
