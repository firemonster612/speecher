#include "platform/PopupInputRegion.h"

#include <QGuiApplication>
#include <QRect>
#include <QRegion>
#include <QWindow>

// After Qt's headers, which define Q_OS_MACOS.
#ifdef Q_OS_MACOS
#include "platform/mac/MacClickThrough.h"
#endif

#ifdef SPEECHER_WITH_X11
// Last: Xlib's macros (None, Status, Bool) would clash with Qt's headers.
#include <X11/Xlib.h>
#include <X11/extensions/shape.h>
#endif

namespace speecher {

namespace {

#ifdef SPEECHER_WITH_X11
// Only rect takes the pointer; the window's shape, which also clips what is
// drawn, stays whole.
void setX11InputShape(Display *display, const QWindow &window, const QRect &rect)
{
    const qreal ratio = window.devicePixelRatio();
    const QRect native = QRectF(QPointF(rect.topLeft()) * ratio, QSizeF(rect.size()) * ratio).toAlignedRect();
    XRectangle shape{short(native.x()), short(native.y()), ushort(native.width()), ushort(native.height())};
    XShapeCombineRectangles(display, window.winId(), ShapeInput, 0, 0, &shape, 1, ShapeSet, Unsorted);
    XFlush(display);
}
#endif

} // namespace

void setPopupInputRegion(QWindow &window, const QRect &inputRegion, const QRect &shadowRegion)
{
#if defined(Q_OS_MACOS)
    // Cocoa's mask clips drawing too, which cut the shadow off.
    Q_UNUSED(shadowRegion);
    mac::takeClicksOnlyIn(&window, inputRegion);
#elif defined(Q_OS_WIN)
    // The mask is the window region, which clips drawing too, so it takes in
    // the shadow. A click on the shadow, a ring 20 pixels wide at the sides,
    // 12 above the capsule and 28 below it, stays with the popup.
    Q_UNUSED(inputRegion);
    window.setMask(QRegion(shadowRegion));
#else
    Q_UNUSED(shadowRegion);
#ifdef SPEECHER_WITH_X11
    // On X11 the mask would be the window's shape, cutting off the shadow and
    // the capsule wherever it moves past the region, so only the input shape
    // is set. By name: offscreen also answers to QX11Application, with no
    // display.
    if (QGuiApplication::platformName() == QLatin1String("xcb")) {
        setX11InputShape(qGuiApp->nativeInterface<QNativeInterface::QX11Application>()->display(), window, inputRegion);
        return;
    }
#endif
    // On Wayland the mask is the surface's input region.
    window.setMask(QRegion(inputRegion));
#endif
}

} // namespace speecher
