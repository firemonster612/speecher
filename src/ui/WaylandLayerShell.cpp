#include "ui/WaylandLayerShell.h"

#include <QGuiApplication>
#include <QScreen>
#include <QWidget>
#include <QWindow>

#ifdef SPEECHER_WITH_LAYER_SHELL
#include <LayerShellQt/Window>
#endif

namespace speecher {

WaylandLayerShell::WaylandLayerShell(QObject *parent)
    : PopupPositioner(parent)
{
}

void WaylandLayerShell::configurePopup(QWidget *widget)
{
    if (!widget) {
        return;
    }
    widget->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    widget->setAttribute(Qt::WA_ShowWithoutActivating);
#ifdef SPEECHER_WITH_LAYER_SHELL
    widget->winId();
    if (auto *window = LayerShellQt::Window::get(widget->windowHandle())) {
        window->setScope(QStringLiteral("speecher-popup"));
        window->setLayer(LayerShellQt::Window::LayerOverlay);
        window->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
#ifdef SPEECHER_LAYER_SHELL_HAS_ACTIVATE_ON_SHOW
        window->setActivateOnShow(false);
#endif
        window->setAnchors(LayerShellQt::Window::AnchorBottom);
        window->setMargins(QMargins(0, 0, 0, 28));
#ifdef SPEECHER_LAYER_SHELL_HAS_DESIRED_SIZE
        window->setDesiredSize(widget->sizeHint());
#endif
    }
#endif
}

void WaylandLayerShell::positionBottomCenter(QWidget *widget)
{
    if (!widget) {
        return;
    }
    const QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        return;
    }
    const QRect area = screen->availableGeometry();
    const QSize size = widget->sizeHint();
    widget->resize(size);
#ifdef SPEECHER_WITH_LAYER_SHELL
    if (auto *window = LayerShellQt::Window::get(widget->windowHandle())) {
#ifdef SPEECHER_LAYER_SHELL_HAS_DESIRED_SIZE
        window->setDesiredSize(size);
#endif
#ifdef SPEECHER_LAYER_SHELL_HAS_WINDOW_SCREEN
        window->setScreen(const_cast<QScreen *>(screen));
#else
        if (QWindow *handle = widget->windowHandle()) {
            handle->setScreen(const_cast<QScreen *>(screen));
        }
#endif
        return;
    }
#endif
    widget->move(area.center().x() - size.width() / 2, area.bottom() - size.height() - 28);
}

} // namespace speecher
