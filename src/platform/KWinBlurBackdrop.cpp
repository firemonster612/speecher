#include "platform/KWinBlurBackdrop.h"

#include <QEvent>
#include <QGuiApplication>
#include <QPainterPath>
#include <QPlatformSurfaceEvent>
#include <QWindow>

#ifdef SPEECHER_WITH_KWIN_BLUR
#include "ext-background-effect-v1-client-protocol.h"
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
    KWinBlurBackdrop *owner = nullptr;
    wl_compositor *compositor = nullptr;
    wl_registry *registry = nullptr;
    ext_background_effect_manager_v1 *effects = nullptr;
    // Whether the effects manager says it can blur now.
    bool effectsBlur = false;
    org_kde_kwin_blur_manager *manager = nullptr;
    // The managers' globals, to know them when the compositor removes them.
    uint32_t effectsName = 0;
    uint32_t managerName = 0;

    bool usesEffects() const { return effects && effectsBlur; }
#endif
};

#ifdef SPEECHER_WITH_KWIN_BLUR
namespace {

QNativeInterface::QWaylandApplication *qtWayland()
{
    return qGuiApp ? qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>() : nullptr;
}

wl_surface *qtSurfaceOf(QWindow *window)
{
    return static_cast<wl_surface *>(
        QGuiApplication::platformNativeInterface()->nativeResourceForWindow("surface", window));
}

// The caller destroys it once the blur has taken it.
wl_region *waylandRegion(wl_compositor *compositor, const QRegion &area)
{
    wl_region *region = wl_compositor_create_region(compositor);
    for (const QRect &rect : area) {
        wl_region_add(region, rect.x(), rect.y(), rect.width(), rect.height());
    }
    return region;
}

} // namespace

KWinBlurBackdrop::KWinBlurBackdrop(QObject *parent)
    : KWinBlurBackdrop(qtWayland() ? qtWayland()->display() : nullptr, qtWayland() ? qtWayland()->compositor() : nullptr,
                       qtSurfaceOf, parent)
{
}
#else
KWinBlurBackdrop::KWinBlurBackdrop(QObject *parent)
    : KWinBlurBackdrop(nullptr, nullptr, {}, parent)
{
}
#endif

KWinBlurBackdrop::KWinBlurBackdrop(wl_display *display, wl_compositor *compositor, SurfaceOf surfaceOf, QObject *parent)
    : WindowBackdrop(parent)
    , m_wayland(std::make_unique<Wayland>())
    , m_surfaceOf(std::move(surfaceOf))
{
#ifdef SPEECHER_WITH_KWIN_BLUR
    if (!display) {
        return;
    }
    m_wayland->owner = this;
    m_wayland->compositor = compositor;
    // The effects manager says whether it can blur as it is bound and again
    // as that changes, as KWin's Blur effect goes off and on.
    static const ext_background_effect_manager_v1_listener effectsListener = {
        .capabilities = [](void *data, ext_background_effect_manager_v1 *, uint32_t flags) {
            auto *wayland = static_cast<Wayland *>(data);
            const bool blur = flags & EXT_BACKGROUND_EFFECT_MANAGER_V1_CAPABILITY_BLUR;
            if (blur != wayland->effectsBlur) {
                wayland->effectsBlur = blur;
                wayland->owner->blurChanged();
            }
        },
    };
    // KWin before 6.7 adds its blur manager while its Blur effect is on and
    // removes it when it goes off, so the registry stays to hear both.
    static const wl_registry_listener listener = {
        .global = [](void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t) {
            auto *wayland = static_cast<Wayland *>(data);
            if (!wayland->effects && std::strcmp(interface, ext_background_effect_manager_v1_interface.name) == 0) {
                wayland->effects = static_cast<ext_background_effect_manager_v1 *>(
                    wl_registry_bind(registry, name, &ext_background_effect_manager_v1_interface, 1));
                wayland->effectsName = name;
                ext_background_effect_manager_v1_add_listener(wayland->effects, &effectsListener, wayland);
            } else if (!wayland->manager && std::strcmp(interface, org_kde_kwin_blur_manager_interface.name) == 0) {
                wayland->manager = static_cast<org_kde_kwin_blur_manager *>(
                    wl_registry_bind(registry, name, &org_kde_kwin_blur_manager_interface, 1));
                wayland->managerName = name;
                wayland->owner->blurChanged();
            }
        },
        .global_remove = [](void *data, wl_registry *, uint32_t name) {
            auto *wayland = static_cast<Wayland *>(data);
            if (wayland->effects && name == wayland->effectsName) {
                ext_background_effect_manager_v1_destroy(wayland->effects);
                wayland->effects = nullptr;
                wayland->effectsBlur = false;
                wayland->owner->blurChanged();
            } else if (wayland->manager && name == wayland->managerName) {
                org_kde_kwin_blur_manager_destroy(wayland->manager);
                wayland->manager = nullptr;
                wayland->owner->blurChanged();
            }
        },
    };
    // The first look is on a queue of its own, so the round trips dispatch
    // nothing of Qt's: one for the globals, one for what the effects manager
    // can do. Then the registry and managers move to the default queue,
    // which Qt dispatches on this thread.
    wl_event_queue *queue = wl_display_create_queue(display);
    auto *wrapper = static_cast<wl_display *>(wl_proxy_create_wrapper(display));
    wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(wrapper), queue);
    m_wayland->registry = wl_display_get_registry(wrapper);
    wl_proxy_wrapper_destroy(wrapper);
    wl_registry_add_listener(m_wayland->registry, &listener, m_wayland.get());
    wl_display_roundtrip_queue(display, queue);
    if (m_wayland->effects) {
        wl_display_roundtrip_queue(display, queue);
        wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(m_wayland->effects), nullptr);
    }
    wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(m_wayland->registry), nullptr);
    if (m_wayland->manager) {
        wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(m_wayland->manager), nullptr);
    }
    // Whatever arrived for the registry since the round trip.
    wl_display_dispatch_queue_pending(display, queue);
    wl_event_queue_destroy(queue);
#else
    Q_UNUSED(display);
    Q_UNUSED(compositor);
#endif
}

KWinBlurBackdrop::~KWinBlurBackdrop()
{
    for (Blur &blur : m_windows) {
        release(blur);
    }
#ifdef SPEECHER_WITH_KWIN_BLUR
    if (m_wayland->effects) {
        ext_background_effect_manager_v1_destroy(m_wayland->effects);
    }
    if (m_wayland->manager) {
        org_kde_kwin_blur_manager_destroy(m_wayland->manager);
    }
    if (m_wayland->registry) {
        wl_registry_destroy(m_wayland->registry);
    }
#endif
}

void KWinBlurBackdrop::blurChanged()
{
    const bool blurs = available();
    for (auto it = m_windows.begin(); it != m_windows.end(); ++it) {
        release(*it);
        // An expose before set() asks again must not bring back a stale
        // region.
        if (!blurs) {
            it->region = {};
        } else if (!it->region.isEmpty()) {
            apply(it.key());
        }
    }
    emit availableChanged();
}

bool KWinBlurBackdrop::available() const
{
#ifdef SPEECHER_WITH_KWIN_BLUR
    return m_wayland->usesEffects() || m_wayland->manager;
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
    // Without a manager that blurs there is nothing to blur with; KWin may
    // take it away while a window still shows.
    if (!available()) {
        return;
    }
    Blur &blur = m_windows[window];
    wl_surface *surface = m_surfaceOf(window);
    // Without a surface yet, the next expose applies it.
    if (!surface) {
        return;
    }
    if (blur.surface != surface) {
        release(blur);
        blur.surface = surface;
    }
    if (m_wayland->usesEffects()) {
        if (blur.region.isEmpty()) {
            // Destroying it takes the blur away with the next commit.
            release(blur);
            blur.surface = surface;
        } else {
            if (!blur.effect) {
                blur.effect = ext_background_effect_manager_v1_get_background_effect(m_wayland->effects, surface);
            }
            wl_region *region = waylandRegion(m_wayland->compositor, blur.region);
            ext_background_effect_surface_v1_set_blur_region(blur.effect, region);
            wl_region_destroy(region);
        }
    } else if (blur.region.isEmpty()) {
        if (blur.blur) {
            org_kde_kwin_blur_manager_unset(m_wayland->manager, surface);
            release(blur);
            blur.surface = surface;
        }
    } else {
        if (!blur.blur) {
            blur.blur = org_kde_kwin_blur_manager_create(m_wayland->manager, surface);
        }
        wl_region *region = waylandRegion(m_wayland->compositor, blur.region);
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
    if (blur.effect) {
        ext_background_effect_surface_v1_destroy(blur.effect);
    }
    if (blur.blur) {
        org_kde_kwin_blur_release(blur.blur);
    }
#endif
    blur.effect = nullptr;
    blur.blur = nullptr;
    blur.surface = nullptr;
}

} // namespace speecher
