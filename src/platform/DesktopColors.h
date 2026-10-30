#pragma once

#include "core/ColorSchemes.h"

#include <QObject>

namespace speecher {

// The desktop's own colour scheme, as the person last chose it, followed as
// they change it.
class DesktopColors : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    // The scheme in effect; invalid where the desktop keeps its defaults,
    // which it leaves unwritten.
    virtual ColorScheme scheme() const = 0;

signals:
    // The scheme may read differently now.
    void changed();
};

} // namespace speecher
