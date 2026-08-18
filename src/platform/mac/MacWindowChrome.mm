#include "platform/mac/MacWindowChrome.h"

#include <QWidget>

#import <AppKit/AppKit.h>

namespace speecher::mac {
namespace {

// Identifiers tag the injected panels so a second call finds the existing view
// instead of stacking another blur behind the window.
NSString *const kSidebarPanelIdentifier = @"speecher.sidebarVibrancy";
NSString *const kPopupPanelIdentifier = @"speecher.popupVibrancy";

constexpr CGFloat kPopupCornerRadius = 16.0;

NSWindow *nativeWindow(QWidget *widget)
{
    if (!widget) {
        return nil;
    }
    // winId() forces the native window into existence; before that there is
    // nothing to decorate.
    NSView *view = reinterpret_cast<NSView *>(widget->winId());
    return view.window;
}

// The window's frame view is the only place a sibling can sit *behind* Qt's
// content view. Returns nil before the native window exists.
NSView *frameView(NSWindow *window)
{
    return window.contentView.superview;
}

NSVisualEffectView *panelWithIdentifier(NSView *parent, NSString *identifier)
{
    for (NSView *child in parent.subviews) {
        if ([child isKindOfClass:[NSVisualEffectView class]]
            && [child.identifier isEqualToString:identifier]) {
            return static_cast<NSVisualEffectView *>(child);
        }
    }
    return nil;
}

NSVisualEffectView *insertPanelBehindContent(NSWindow *window,
                                             NSVisualEffectMaterial material,
                                             NSRect frame,
                                             NSAutoresizingMaskOptions resizing,
                                             NSString *identifier)
{
    NSView *parent = frameView(window);
    if (!parent) {
        return nil;
    }
    if (NSVisualEffectView *existing = panelWithIdentifier(parent, identifier)) {
        return existing;
    }
    NSVisualEffectView *panel = [[NSVisualEffectView alloc] initWithFrame:frame];
    panel.identifier = identifier;
    panel.material = material;
    panel.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    panel.state = NSVisualEffectStateFollowsWindowActiveState;
    panel.autoresizingMask = resizing;
    [parent addSubview:panel positioned:NSWindowBelow relativeTo:window.contentView];
    // addSubview: retains, and this file compiles without ARC, so the +1 from
    // alloc is ours to drop. The superview keeps the panel alive.
    [panel release];
    return panel;
}

} // namespace

void applyMainWindowChrome(QWidget *window, int sidebarWidth)
{
    NSWindow *native = nativeWindow(window);
    NSView *parent = frameView(native);
    if (!parent) {
        return;
    }
    native.titlebarAppearsTransparent = YES;
    native.styleMask |= NSWindowStyleMaskFullSizeContentView;
    native.titleVisibility = NSWindowTitleHidden;

    insertPanelBehindContent(native,
                             NSVisualEffectMaterialSidebar,
                             NSMakeRect(0, 0, sidebarWidth, NSHeight(parent.bounds)),
                             NSViewHeightSizable,
                             kSidebarPanelIdentifier);
}

void updateSidebarWidth(QWidget *window, int sidebarWidth)
{
    NSView *parent = frameView(nativeWindow(window));
    NSVisualEffectView *panel =
        parent ? panelWithIdentifier(parent, kSidebarPanelIdentifier) : nil;
    if (!panel) {
        return;
    }
    NSRect frame = panel.frame;
    frame.size.width = sidebarWidth;
    panel.frame = frame;
}

void applyPopupChrome(QWidget *popup)
{
    NSWindow *native = nativeWindow(popup);
    NSView *parent = frameView(native);
    if (!parent) {
        return;
    }
    native.opaque = NO;
    native.backgroundColor = NSColor.clearColor;

    NSVisualEffectView *panel = insertPanelBehindContent(
        native,
        NSVisualEffectMaterialHUDWindow,
        parent.bounds,
        NSViewWidthSizable | NSViewHeightSizable,
        kPopupPanelIdentifier);
    if (!panel) {
        return;
    }
    panel.wantsLayer = YES;
    panel.layer.cornerRadius = kPopupCornerRadius;
    panel.layer.masksToBounds = YES;
}

void applyAppearance(Qt::ColorScheme scheme)
{
    NSAppearance *appearance = nil;
    if (scheme == Qt::ColorScheme::Light) {
        appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
    } else if (scheme == Qt::ColorScheme::Dark) {
        appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    }
    NSApp.appearance = appearance;
}

} // namespace speecher::mac
