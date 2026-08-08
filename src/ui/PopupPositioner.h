#pragma once

#include <QObject>

class QWidget;

namespace speecher {

class PopupPositioner : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    virtual void configurePopup(QWidget *widget) = 0;
    virtual void positionBottomCenter(QWidget *widget) = 0;
};

} // namespace speecher
