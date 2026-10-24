#include "platform/mac/MacWindowBackdrop.h"

#include <QRect>
#include <QWindow>

#import <AppKit/AppKit.h>

namespace speecher {

namespace {

NSString *const backdropIdentifier = @"speecher.backdrop";

// The window carrying window's effect, or nil.
NSWindow *backdropOf(NSWindow *window)
{
    for (NSWindow *child in window.childWindows) {
        if ([child.identifier isEqualToString:backdropIdentifier]) {
            return child;
        }
    }
    return nil;
}

// A borderless window that is nothing but the effect, which clicks pass.
NSWindow *makeBackdrop()
{
    NSWindow *backdrop = [[NSWindow alloc] initWithContentRect:NSZeroRect
                                                     styleMask:NSWindowStyleMaskBorderless
                                                       backing:NSBackingStoreBuffered
                                                         defer:NO];
    backdrop.identifier = backdropIdentifier;
    backdrop.releasedWhenClosed = NO;
    backdrop.opaque = NO;
    backdrop.backgroundColor = NSColor.clearColor;
    backdrop.hasShadow = NO;
    backdrop.ignoresMouseEvents = YES;
    NSVisualEffectView *effect = [[NSVisualEffectView alloc] init];
    effect.material = NSVisualEffectMaterialSidebar;
    effect.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    // It is never the key window itself; the window above it is.
    effect.state = NSVisualEffectStateActive;
    backdrop.contentView = effect;
    return backdrop;
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

// The effect is a child window kept just below window, which moves with
// it; Qt's view stays the window's content view, as Qt needs, and AppKit's
// frame view is left alone.
void MacWindowBackdrop::set(QWindow *window, const QRect &area, int radius, bool dark)
{
    if (!window) {
        return;
    }
    NSView *view = (__bridge NSView *)reinterpret_cast<void *>(window->winId());
    NSWindow *parent = view.window;
    // Without an NSWindow yet there is nothing to sit under.
    if (!parent) {
        return;
    }
    NSWindow *backdrop = backdropOf(parent);
    if (area.isEmpty()) {
        if (backdrop) {
            [parent removeChildWindow:backdrop];
            [backdrop orderOut:nil];
        }
        return;
    }
    if (!backdrop) {
        backdrop = makeBackdrop();
        [parent addChildWindow:backdrop ordered:NSWindowBelow];
    }
    auto *effect = static_cast<NSVisualEffectView *>(backdrop.contentView);
    effect.appearance = [NSAppearance appearanceNamed:dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
    effect.maskImage = roundedMask(radius);
    // Qt's view is flipped, so area converts as Qt measures it.
    const NSRect inWindow = [view convertRect:NSMakeRect(area.x(), area.y(), area.width(), area.height()) toView:nil];
    [backdrop setFrame:[parent convertRectToScreen:inWindow] display:YES];
}

} // namespace speecher
