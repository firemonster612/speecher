#pragma once

#include "ui/PopupPositioner.h"

class QWidget;

namespace speecher {

class WaylandLayerShell : public PopupPositioner {
    Q_OBJECT

public:
    explicit WaylandLayerShell(QObject *parent = nullptr);
    void configurePopup(QWidget *widget) override;
    void positionBottomCenter(QWidget *widget) override;
};

} // namespace speecher
