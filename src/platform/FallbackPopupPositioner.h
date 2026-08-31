#pragma once

#include "platform/PopupPositioner.h"

class QScreen;

namespace speecher {

// Places the popup with plain geometry. Every platform can do this much, so
// platform positioners extend it rather than reimplement it.
class FallbackPopupPositioner : public PopupPositioner {
    Q_OBJECT

public:
    using PopupPositioner::PopupPositioner;

    void positionBottomCenter(PopupSurface &surface) override;

protected:
    // Subclasses that pick a different screen still get the same placement.
    void positionBottomCenterOn(PopupSurface &surface, const QScreen *screen);
};

} // namespace speecher
