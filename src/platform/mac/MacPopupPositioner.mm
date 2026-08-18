#include "platform/mac/MacPopupPositioner.h"

#include <QWidget>

#import <AppKit/AppKit.h>

namespace speecher {

MacPopupPositioner::MacPopupPositioner(QObject *parent)
    : FallbackPopupPositioner(parent)
{
}

void MacPopupPositioner::configurePopup(QWidget *widget)
{
    FallbackPopupPositioner::configurePopup(widget);
    if (!widget) {
        return;
    }

    // winId() forces the native window into existence; before that there is no
    // NSWindow to harden.
    NSView *view = reinterpret_cast<NSView *>(widget->winId());
    NSWindow *window = view.window;
    if (!window) {
        return;
    }

    // Qt::Tool backs the popup with an NSPanel, and only a panel accepts the
    // non-activating style mask that keeps the dictation target focused.
    if ([window isKindOfClass:[NSPanel class]]) {
        window.styleMask |= NSWindowStyleMaskNonactivatingPanel;
    }
    window.level = NSStatusWindowLevel;
    window.collectionBehavior |= NSWindowCollectionBehaviorCanJoinAllSpaces
        | NSWindowCollectionBehaviorFullScreenAuxiliary
        | NSWindowCollectionBehaviorStationary;
    window.animationBehavior = NSWindowAnimationBehaviorNone;
}

} // namespace speecher
