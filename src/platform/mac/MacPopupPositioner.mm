#include "platform/mac/MacPopupPositioner.h"

#include "platform/PopupSurface.h"

#include <QCursor>
#include <QDebug>
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>

#import <AppKit/AppKit.h>

namespace speecher {

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

void MacPopupPositioner::positionBottomCenter(PopupSurface &surface)
{
    // The popup belongs on the display the user is working on, which on a
    // multi-display Mac is often not the primary one.
    const QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    positionBottomCenterOn(surface, screen ? screen : QGuiApplication::primaryScreen());
}

} // namespace speecher
