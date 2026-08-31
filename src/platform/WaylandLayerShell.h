#pragma once

#include "platform/FallbackPopupPositioner.h"

namespace speecher {

class WaylandLayerShell : public FallbackPopupPositioner {
    Q_OBJECT

public:
    explicit WaylandLayerShell(QObject *parent = nullptr);
    void configurePopup(PopupSurface &surface) override;
    void positionBottomCenter(PopupSurface &surface) override;
};

} // namespace speecher
