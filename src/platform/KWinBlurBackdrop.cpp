#include "platform/KWinBlurBackdrop.h"

#include <QEvent>
#include <QGuiApplication>
#include <QPainterPath>
#include <QPlatformSurfaceEvent>
#include <QWindow>

#ifdef SPEECHER_WITH_KWIN_BLUR
#include "kde-blur-client-protocol.h"

#include <qpa/qplatformnativeinterface.h>
#include <wayland-client.h>

#include <cstring>
#endif

namespace speecher {

namespace {

// area with its corners rounded off; a blur region is made of rectangles.
QRegion roundedRegion(const QRect &area, int radius)
{
    if (area.isEmpty()) {
        return {};
    }
    QPainterPath path;
    path.addRoundedRect(area, radius, radius);
    return QRegion(path.toFillPolygon().toPolygon());
}

} // namespace

struct KWinBlurBackdrop::Wayland {
#ifdef SPEECHER_WITH_KWIN_BLUR
    wl_compositor *compositor = nullptr;
    org_kde_kwin_blur_manager *manager = nullptr;
#endif
};

KWinBlurBackdrop::KWinBlurBackdrop(QObject *parent)
    : WindowBackdrop(parent)
    , m_wayland(std::make_unique<Wayland>())
{
#ifdef SPEECHER_WITH_KWIN_BLUR
    auto *app = qGuiApp ? qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>() : nullptr;
    if (!app) {
        return;
    }
    m_wayland->compositor = app->compositor();
    // Looks for KWin's blur manager on a queue of its own, so the round trip
    // dispatches nothing of Qt's.
    wl_display *display = app->display();
    wl_event_queue *queue = wl_display_create_queue(display);
    auto *wrapper = static_cast<wl_display *>(wl_proxy_create_wrapper(display));
    wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(wrapper), queue);
    wl_registry *registry = wl_display_get_registry(wrapper);
    static const wl_registry_listener listener = {
        .global = [](void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t) {
            if (std::strcmp(interface, org_kde_kwin_blur_manager_interface.name) == 0) {
                static_cast<Wayland *>(data)->manager = static_cast<org_kde_kwin_blur_manager *>(
                    wl_registry_bind(registry, name, &org_kde_kwin_blur_manager_interface, 1));
            }
        },
        .global_remove = [](void *, wl_registry *, uint32_t) {},
    };
    wl_registry_add_listener(registry, &listener, m_wayland.get());
    wl_display_roundtrip_queue(display, queue);
    // The manager has no events; it goes back to Qt's queue before ours goes.
    if (m_wayland->manager) {
        wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(m_wayland->manager), nullptr);
    }
    wl_registry_destroy(registry);
    wl_proxy_wrapper_destroy(wrapper);
    wl_event_queue_destroy(queue);
#endif
}

KWinBlurBackdrop::~KWinBlurBackdrop()
{
    for (Blur &blur : m_windows) {
        release(blur);
    }
#ifdef SPEECHER_WITH_KWIN_BLUR
    if (m_wayland->manager) {
        org_kde_kwin_blur_manager_destroy(m_wayland->manager);
    }
#endif
}

bool KWinBlurBackdrop::available() const
{
#ifdef SPEECHER_WITH_KWIN_BLUR
    return m_wayland->manager != nullptr;
#else
    return false;
#endif
}

// KWin's blur has no material of its own; dark is the UI's to draw.
void KWinBlurBackdrop::set(QWindow *window, const QRect &area, int radius, bool)
{
    if (!window || !available()) {
        return;
    }
    if (!m_windows.contains(window)) {
        window->installEventFilter(this);
        connect(window, &QObject::destroyed, this, [this, window] { m_windows.remove(window); });
    }
    m_windows[window].region = roundedRegion(area, radius);
    apply(window);
}

bool KWinBlurBackdrop::eventFilter(QObject *watched, QEvent *event)
{
    auto *window = static_cast<QWindow *>(watched);
    if (event->type() == QEvent::Expose && window->isExposed()) {
        apply(window);
    } else if (event->type() == QEvent::PlatformSurface
               && static_cast<QPlatformSurfaceEvent *>(event)->surfaceEventType()
                      == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
        release(m_windows[window]);
    }
    return false;
}

void KWinBlurBackdrop::apply(QWindow *window)
{
#ifdef SPEECHER_WITH_KWIN_BLUR
    Blur &blur = m_windows[window];
    auto *surface = static_cast<wl_surface *>(
        QGuiApplication::platformNativeInterface()->nativeResourceForWindow("surface", window));
    // Without a surface yet, the next expose applies it.
    if (!surface) {
        return;
    }
    if (blur.surface != surface) {
        release(blur);
        blur.surface = surface;
    }
    if (blur.region.isEmpty()) {
        if (blur.blur) {
            org_kde_kwin_blur_manager_unset(m_wayland->manager, surface);
            release(blur);
            blur.surface = surface;
        }
    } else {
        if (!blur.blur) {
            blur.blur = org_kde_kwin_blur_manager_create(m_wayland->manager, surface);
        }
        wl_region *region = wl_compositor_create_region(m_wayland->compositor);
        for (const QRect &rect : blur.region) {
            wl_region_add(region, rect.x(), rect.y(), rect.width(), rect.height());
        }
        org_kde_kwin_blur_set_region(blur.blur, region);
        org_kde_kwin_blur_commit(blur.blur);
        wl_region_destroy(region);
    }
    // The blur takes effect with the surface's next commit: the next frame.
    window->requestUpdate();
#else
    Q_UNUSED(window);
#endif
}

void KWinBlurBackdrop::release(Blur &blur)
{
#ifdef SPEECHER_WITH_KWIN_BLUR
    if (blur.blur) {
        org_kde_kwin_blur_release(blur.blur);
    }
#endif
    blur.blur = nullptr;
    blur.surface = nullptr;
}

} // namespace speecher
