#pragma once

#include "ui/PopupPositioner.h"

class QScreen;
class QWidget;

namespace speecher {

// Places the popup with plain Qt window flags and geometry. Every platform can
// do this much, so platform positioners extend it rather than reimplement it.
class FallbackPopupPositioner : public PopupPositioner {
    Q_OBJECT

public:
    using PopupPositioner::PopupPositioner;

    void configurePopup(QWidget *widget) override;
    void positionBottomCenter(QWidget *widget) override;

protected:
    // Subclasses that pick a different screen still get the same placement.
    void positionBottomCenterOn(QWidget *widget, const QScreen *screen);
};

} // namespace speecher
