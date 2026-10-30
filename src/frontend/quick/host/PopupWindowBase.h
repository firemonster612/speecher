#pragma once

// The C++ side of Speecher.Popup's PopupWindow: the window the popup draws
// in, with what the host follows typed, so the host reads no QML property by
// name.

#include <QQmlEngine>
#include <QQuickWindow>
#include <QRectF>

namespace speecher {

class PopupWindowBase : public QQuickWindow {
    Q_OBJECT
    QML_ELEMENT
    // What takes the pointer, in the window's coordinates.
    Q_PROPERTY(QRectF inputRegion MEMBER m_inputRegion NOTIFY inputRegionChanged)
    // The input region and as far as the capsule's shadow reaches: where
    // Windows' mask, which also clips drawing, has to reach.
    Q_PROPERTY(QRectF shadowRegion MEMBER m_shadowRegion NOTIFY shadowRegionChanged)
    // The popup has faded out, so the host hides the window.
    Q_PROPERTY(bool empty MEMBER m_empty NOTIFY emptyChanged)

public:
    QRectF inputRegion() const { return m_inputRegion; }
    QRectF shadowRegion() const { return m_shadowRegion; }
    bool empty() const { return m_empty; }

signals:
    void inputRegionChanged();
    void shadowRegionChanged();
    void emptyChanged();

private:
    QRectF m_inputRegion;
    QRectF m_shadowRegion;
    bool m_empty = false;
};

} // namespace speecher
