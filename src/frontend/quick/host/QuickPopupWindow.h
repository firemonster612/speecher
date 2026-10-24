#pragma once

#include "platform/PopupSurface.h"

#include <QObject>

class QQmlEngine;
class QQuickWindow;
class QTimer;

namespace speecher {

class PopupPositioner;

// The dictation popup's window in the daemon: Speecher.Popup's PopupWindow,
// placed by the platform's positioner (an overlay layer-shell surface at the
// bottom of the active screen on Wayland, plain geometry elsewhere) and never
// focused or activated, so the Target keeps the keyboard. Only the capsule
// takes the pointer. It shows when asked and hides once the popup has faded
// out.
class QuickPopupWindow final : public QObject, private PopupSurface {
    Q_OBJECT

public:
    // Takes the positioner. Logs and stays empty if the window cannot load.
    QuickPopupWindow(QQmlEngine *engine, PopupPositioner *positioner, QObject *parent = nullptr);
    ~QuickPopupWindow() override;

    // Shows the popup and, once a frame of it is on screen, says presented
    // for this generation; a later show or a hide first drops it. Generation
    // 0 is a popup nothing waits for, such as an error from outside a
    // session.
    void show(quint64 generation);
    void hide();

signals:
    void presented(quint64 generation);

private slots:
    // Follow PopupWindow's properties of the same names.
    void applyInputRegion();
    void hideOnceEmpty();

private:
    QSize preferredSize() const override;
    void resizeTo(const QSize &size) override;
    void moveTo(const QPoint &topLeft) override;
    QWindow *nativeWindow() override;

    void framePresented(quint64 generation);
    void captureWhenAsked();

    QQuickWindow *m_window = nullptr;
    PopupPositioner *m_positioner;
    // The show waiting for its first frame, 0 for none.
    quint64 m_pendingGeneration = 0;
    // The render thread's: the show the frame being drawn was synchronized
    // after.
    quint64 m_syncedGeneration = 0;
};

} // namespace speecher
