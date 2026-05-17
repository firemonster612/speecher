#pragma once

#include <QObject>

class QWidget;

namespace speecher {

class WaylandLayerShell : public QObject {
    Q_OBJECT

public:
    explicit WaylandLayerShell(QObject *parent = nullptr);
    void configurePopup(QWidget *widget);
    void positionBottomCenter(QWidget *widget);
};

} // namespace speecher
