#include "platform/FallbackPopupPositioner.h"

#include "platform/PopupSurface.h"

#include <QGuiApplication>
#include <QRect>
#include <QScreen>

namespace speecher {
namespace {

constexpr int bottomMarginPx = 28;

} // namespace

void FallbackPopupPositioner::positionBottomCenter(PopupSurface &surface)
{
    positionBottomCenterOn(surface, QGuiApplication::primaryScreen());
}

void FallbackPopupPositioner::positionBottomCenterOn(PopupSurface &surface, const QScreen *screen)
{
    if (!screen) {
        return;
    }
    const QRect area = screen->availableGeometry();
    const QSize size = surface.preferredSize();
    surface.resizeTo(size);
    surface.moveTo({area.center().x() - size.width() / 2,
                    area.bottom() - size.height() - bottomMarginPx});
}

} // namespace speecher
