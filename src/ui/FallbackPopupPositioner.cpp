#include "ui/FallbackPopupPositioner.h"

#include <QGuiApplication>
#include <QScreen>
#include <QWidget>

namespace speecher {
namespace {

constexpr int bottomMarginPx = 28;

} // namespace

void FallbackPopupPositioner::configurePopup(QWidget *widget)
{
    if (!widget) {
        return;
    }
    widget->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    widget->setAttribute(Qt::WA_ShowWithoutActivating);
}

void FallbackPopupPositioner::positionBottomCenter(QWidget *widget)
{
    positionBottomCenterOn(widget, QGuiApplication::primaryScreen());
}

void FallbackPopupPositioner::positionBottomCenterOn(QWidget *widget, const QScreen *screen)
{
    if (!widget) {
        return;
    }
    if (!screen) {
        return;
    }
    const QRect area = screen->availableGeometry();
    const QSize size = widget->sizeHint();
    widget->resize(size);
    widget->move(area.center().x() - size.width() / 2,
                 area.bottom() - size.height() - bottomMarginPx);
}

} // namespace speecher
