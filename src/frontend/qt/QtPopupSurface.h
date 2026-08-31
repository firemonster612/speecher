#pragma once

#include "platform/PopupSurface.h"

class QWidget;

namespace speecher {

// Presents a widget to the platform's popup positioners as a plain native
// window, so that the positioners never see Qt Widgets.
class QtPopupSurface final : public PopupSurface {
public:
    explicit QtPopupSurface(QWidget *widget);

    QSize preferredSize() const override;
    void resizeTo(const QSize &size) override;
    void moveTo(const QPoint &topLeft) override;
    QWindow *nativeWindow() override;

private:
    QWidget *m_widget;
};

} // namespace speecher
