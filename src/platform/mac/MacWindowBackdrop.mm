#include "platform/mac/MacWindowBackdrop.h"

#include <QRect>
#include <QWindow>

#import <AppKit/AppKit.h>

namespace speecher {

namespace {

NSString *const backdropIdentifier = @"speecher.backdrop";

// The effect view set under view, or nil.
NSVisualEffectView *backdropUnder(NSView *view)
{
    for (NSView *sibling in view.superview.subviews) {
        if ([sibling.identifier isEqualToString:backdropIdentifier]) {
            return static_cast<NSVisualEffectView *>(sibling);
        }
    }
    return nil;
}

// A mask that rounds the effect view's corners at any size.
NSImage *roundedMask(int radius)
{
    const CGFloat side = radius * 2 + 1;
    NSImage *mask = [NSImage imageWithSize:NSMakeSize(side, side)
                                   flipped:NO
                            drawingHandler:^BOOL(NSRect rect) {
                                [[NSBezierPath bezierPathWithRoundedRect:rect xRadius:radius yRadius:radius] fill];
                                return YES;
                            }];
    mask.capInsets = NSEdgeInsetsMake(radius, radius, radius, radius);
    mask.resizingMode = NSImageResizingModeStretch;
    return mask;
}

} // namespace

MacWindowBackdrop::MacWindowBackdrop(QObject *parent)
    : WindowBackdrop(parent)
{
    m_observer = (__bridge_retained void *)[[NSWorkspace sharedWorkspace].notificationCenter
        addObserverForName:NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification *) {
                    emit availableChanged();
                }];
}

MacWindowBackdrop::~MacWindowBackdrop()
{
    id observer = (__bridge_transfer id)m_observer;
    [[NSWorkspace sharedWorkspace].notificationCenter removeObserver:observer];
}

bool MacWindowBackdrop::available() const
{
    return ![NSWorkspace sharedWorkspace].accessibilityDisplayShouldReduceTransparency;
}

void MacWindowBackdrop::set(QWindow *window, const QRect &area, int radius, bool dark)
{
    if (!window) {
        return;
    }
    auto *view = reinterpret_cast<NSView *>(window->winId());
    NSVisualEffectView *effect = backdropUnder(view);
    if (area.isEmpty() || !view.superview) {
        [effect removeFromSuperview];
        return;
    }
    if (!effect) {
        effect = [[NSVisualEffectView alloc] init];
        effect.identifier = backdropIdentifier;
        effect.material = NSVisualEffectMaterialSidebar;
        effect.blendingMode = NSVisualEffectBlendingModeBehindWindow;
        effect.state = NSVisualEffectStateFollowsWindowActiveState;
        // Under Qt's view, so the UI draws over it.
        [view.superview addSubview:effect positioned:NSWindowBelow relativeTo:view];
    }
    effect.appearance = [NSAppearance appearanceNamed:dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
    effect.maskImage = roundedMask(radius);
    // Qt's view is flipped; converting places area the same in either.
    effect.frame = [view convertRect:NSMakeRect(area.x(), area.y(), area.width(), area.height())
                              toView:view.superview];
}

} // namespace speecher
