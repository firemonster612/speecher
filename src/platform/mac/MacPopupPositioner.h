#pragma once

#include "ui/FallbackPopupPositioner.h"

class QWidget;

namespace speecher {

class MacPopupPositioner : public FallbackPopupPositioner {
    Q_OBJECT

public:
    explicit MacPopupPositioner(QObject *parent = nullptr);
    void configurePopup(QWidget *widget) override;
    void positionBottomCenter(QWidget *widget) override;
};

} // namespace speecher
