#pragma once

#include "ui/FallbackPopupPositioner.h"

class QWidget;

namespace speecher {

class WaylandLayerShell : public FallbackPopupPositioner {
    Q_OBJECT

public:
    explicit WaylandLayerShell(QObject *parent = nullptr);
    void configurePopup(QWidget *widget) override;
    void positionBottomCenter(QWidget *widget) override;
};

} // namespace speecher
