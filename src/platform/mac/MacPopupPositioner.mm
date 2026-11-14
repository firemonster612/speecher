#include "platform/mac/MacPopupPositioner.h"

#include "platform/PopupSurface.h"

#include <QDebug>
#include <QGuiApplication>
#include <QPlatformSurfaceEvent>
#include <QScreen>
#include <QWindow>

#import <AppKit/AppKit.h>

namespace speecher {

namespace {

void harden(QWindow *handle)
{
    NSView *view = reinterpret_cast<NSView *>(handle->winId());
    NSWindow *window = view.window;
    if (!window) {
        return;
    }

    // Qt::Tool backs the popup with an NSPanel, and only a panel accepts the
    // non-activating style mask that keeps the dictation target focused.
    if ([window isKindOfClass:[NSPanel class]]) {
        window.styleMask |= NSWindowStyleMaskNonactivatingPanel;
    } else {
        qWarning().noquote()
            << "The dictation popup is not an NSPanel, so it cannot take the "
               "non-activating style mask: showing it will pull focus away from "
               "the app being dictated into.";
    }
    // Qt hides tool windows while their app is inactive, and Speecher is
    // never the active app while someone dictates into another.
    window.hidesOnDeactivate = NO;
    window.level = NSStatusWindowLevel;
    // Qt tool windows arrive with MoveToActiveSpace, which AppKit rejects in
    // combination with CanJoinAllSpaces — clear it before joining all spaces.
    window.collectionBehavior = (window.collectionBehavior
                                 & ~NSWindowCollectionBehaviorMoveToActiveSpace)
        | NSWindowCollectionBehaviorCanJoinAllSpaces
        | NSWindowCollectionBehaviorFullScreenAuxiliary
        | NSWindowCollectionBehaviorStationary;
    window.animationBehavior = NSWindowAnimationBehaviorNone;
}

// Each time Qt makes the window's native window again, the new NSPanel comes
// with Qt's defaults: hidden whenever Speecher isn't the active app, and
// taking focus when shown. What makes Qt do this on real Macs is not known
// yet; the log lines are there to find out.
class Rehardener final : public QObject {
public:
    using QObject::QObject;

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::PlatformSurface) {
            return false;
        }
        switch (static_cast<QPlatformSurfaceEvent *>(event)->surfaceEventType()) {
        case QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed:
            qInfo().noquote() << "The dictation popup's native window is going away.";
            break;
        case QPlatformSurfaceEvent::SurfaceCreated:
            qInfo().noquote() << "The dictation popup has a new native window; hardening it again.";
            harden(static_cast<QWindow *>(watched));
            break;
        }
        return false;
    }
};

} // namespace

MacPopupPositioner::MacPopupPositioner(QObject *parent)
    : FallbackPopupPositioner(parent)
{
}

void MacPopupPositioner::configurePopup(PopupSurface &surface)
{
    // winId() only yields an NSView on the cocoa platform; under the offscreen
    // platform (tests) the handle is a foreign type and casting it crashes.
    if (QGuiApplication::platformName() != QLatin1String("cocoa")) {
        return;
    }

    // nativeWindow() forces the native window into existence; before that there
    // is no NSWindow to harden.
    QWindow *handle = surface.nativeWindow();
    if (!handle) {
        return;
    }
    harden(handle);
    handle->installEventFilter(new Rehardener(handle));
}

} // namespace speecher
