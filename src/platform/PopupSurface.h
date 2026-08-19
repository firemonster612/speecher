#pragma once

#include <QPoint>
#include <QSize>

class QWindow;

namespace speecher {

// The dictation popup as a positioner sees it: a native window with a size and
// a place to sit, and no widget toolkit. Naming QWidget here is what used to
// tie the platform layer to Qt Widgets; see
// docs/adr/0001-per-platform-front-ends.md.
class PopupSurface {
public:
    virtual ~PopupSurface() = default;

    // The size the popup wants; positioners resize to it before placing it.
    virtual QSize preferredSize() const = 0;
    virtual void resizeTo(const QSize &size) = 0;
    virtual void moveTo(const QPoint &topLeft) = 0;
    // Forces the native window into existence and returns it. Both the
    // layer-shell and the AppKit positioner harden the real window, which does
    // not exist until something asks for it.
    virtual QWindow *nativeWindow() = 0;
};

} // namespace speecher
