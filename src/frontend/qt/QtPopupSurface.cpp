#include "frontend/qt/QtPopupSurface.h"

#include <QWidget>
#include <QWindow>

namespace speecher {

QtPopupSurface::QtPopupSurface(QWidget *widget)
    : m_widget(widget)
{
}

QSize QtPopupSurface::preferredSize() const
{
    return m_widget->sizeHint();
}

void QtPopupSurface::resizeTo(const QSize &size)
{
    m_widget->resize(size);
}

void QtPopupSurface::moveTo(const QPoint &topLeft)
{
    m_widget->move(topLeft);
}

QWindow *QtPopupSurface::nativeWindow()
{
    // winId() is what creates the native window; windowHandle() is null until
    // something asks for it.
    m_widget->winId();
    return m_widget->windowHandle();
}

} // namespace speecher
