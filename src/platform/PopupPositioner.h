#pragma once

#include <QObject>

namespace speecher {

class PopupSurface;

class PopupPositioner : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    // Called once before the popup is first shown. Doing nothing is the honest
    // default: plain window flags belong to the front end that owns the window.
    virtual void configurePopup(PopupSurface &surface)
    {
        Q_UNUSED(surface);
    }

    virtual void positionBottomCenter(PopupSurface &surface) = 0;
};

} // namespace speecher
