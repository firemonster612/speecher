#include "platform/WaylandLayerShell.h"

#include "platform/PopupSurface.h"

#include <QGuiApplication>
#include <QScreen>
#include <QWindow>

#ifdef SPEECHER_WITH_LAYER_SHELL
#include <LayerShellQt/Window>
#endif

namespace speecher {

WaylandLayerShell::WaylandLayerShell(QObject *parent)
    : FallbackPopupPositioner(parent)
{
}

void WaylandLayerShell::configurePopup(PopupSurface &surface)
{
#ifdef SPEECHER_WITH_LAYER_SHELL
    if (auto *window = LayerShellQt::Window::get(surface.nativeWindow())) {
        window->setScope(QStringLiteral("speecher-popup"));
        window->setLayer(LayerShellQt::Window::LayerOverlay);
        window->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
#ifdef SPEECHER_LAYER_SHELL_HAS_ACTIVATE_ON_SHOW
        window->setActivateOnShow(false);
#endif
        window->setAnchors(LayerShellQt::Window::AnchorBottom);
        window->setMargins(QMargins(0, 0, 0, 28));
#ifdef SPEECHER_LAYER_SHELL_HAS_ACTIVE_SCREEN
        // Wayland gives a background client no way to read the pointer, so
        // the popup asks for the compositor's active screen. On KWin that is
        // the screen with the pointer by default ("Active screen follows mouse"
        // in Window Management); other compositors choose by their own rule.
        window->setWantsToBeOnActiveScreen(true);
#endif
#ifdef SPEECHER_LAYER_SHELL_HAS_DESIRED_SIZE
        window->setDesiredSize(surface.preferredSize());
#endif
    }
#else
    Q_UNUSED(surface);
#endif
}

void WaylandLayerShell::positionBottomCenter(PopupSurface &surface)
{
#ifdef SPEECHER_WITH_LAYER_SHELL
    // The compositor owns the popup's placement once layer-shell accepted it;
    // it only needs the size and, without an active-screen request, the screen.
    QWindow *handle = surface.nativeWindow();
    if (auto *window = LayerShellQt::Window::get(handle)) {
        const QSize size = surface.preferredSize();
        surface.resizeTo(size);
#ifdef SPEECHER_LAYER_SHELL_HAS_DESIRED_SIZE
        window->setDesiredSize(size);
#endif
#ifndef SPEECHER_LAYER_SHELL_HAS_ACTIVE_SCREEN
        auto *screen = const_cast<QScreen *>(pointerScreen());
        if (!screen) {
            return;
        }
#ifdef SPEECHER_LAYER_SHELL_HAS_WINDOW_SCREEN
        window->setScreen(screen);
#else
        if (handle) {
            handle->setScreen(screen);
        }
#endif
#endif
        return;
    }
#endif
    FallbackPopupPositioner::positionBottomCenter(surface);
}

} // namespace speecher
