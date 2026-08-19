#pragma once

#include "platform/FallbackPopupPositioner.h"

namespace speecher {

class MacPopupPositioner : public FallbackPopupPositioner {
    Q_OBJECT

public:
    explicit MacPopupPositioner(QObject *parent = nullptr);
    void configurePopup(PopupSurface &surface) override;
    void positionBottomCenter(PopupSurface &surface) override;
};

} // namespace speecher
