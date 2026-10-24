#pragma once

#include <QObject>

class QRect;
class QWindow;

namespace speecher {

// The desktop seen through a window: the compositor blurs what lies behind
// the part of the window the UI leaves translucent (KWin's blur, macOS's
// vibrancy, Windows 11's Mica). One serves every window of the app.
class WindowBackdrop : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    // Whether a blur shows now: the compositor draws one, and the person
    // has not turned transparency down. While false, set does nothing
    // visible, so the UI should stay opaque.
    virtual bool available() const = 0;
    // Blurs behind area, a rectangle with corners of radius in window's own
    // coordinates, until set again; an empty area takes the blur away. It
    // stays with the window while it is hidden and shown again. dark asks
    // for the platform's dark material where it has one.
    virtual void set(QWindow *window, const QRect &area, int radius, bool dark) = 0;

signals:
    void availableChanged();
};

} // namespace speecher
