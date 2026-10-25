#pragma once

#include "platform/WindowBackdrop.h"

#include <QHash>
#include <QRegion>

#include <functional>
#include <memory>

struct ext_background_effect_surface_v1;
struct org_kde_kwin_blur;
struct wl_compositor;
struct wl_display;
struct wl_surface;

namespace speecher {

// The compositor's blur behind a window on Wayland: through the standard
// ext_background_effect protocol where the compositor offers it with blur
// (KWin 6.7 offers only that), otherwise KWin's older org_kde_kwin_blur.
// Unavailable where the compositor offers neither (X11, KWin with its Blur
// effect off), or where the build lacks them; it follows the compositor
// adding and removing them.
class KWinBlurBackdrop final : public WindowBackdrop {
    Q_OBJECT

public:
    // On Qt's own Wayland connection, or none off Wayland.
    explicit KWinBlurBackdrop(QObject *parent = nullptr);
    // A window's surface on display, or null while it has none.
    using SurfaceOf = std::function<wl_surface *(QWindow *)>;

    // On display, whose default queue someone else dispatches, as Qt does
    // its own; compositor makes the blur regions, surfaceOf finds what to
    // blur behind.
    KWinBlurBackdrop(wl_display *display, wl_compositor *compositor, SurfaceOf surfaceOf, QObject *parent = nullptr);
    ~KWinBlurBackdrop() override;

    bool available() const override;
    void set(QWindow *window, const QRect &area, int radius, bool dark) override;

protected:
    // Applies a window's blur to each new surface Qt makes for it as it is
    // shown, and lets go of it before the surface goes.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Blur {
        QRegion region;
        // The surface the blur was made for, and the blur through one of the
        // protocols; null until the window has a surface.
        wl_surface *surface = nullptr;
        ext_background_effect_surface_v1 *effect = nullptr;
        org_kde_kwin_blur *blur = nullptr;
    };
    struct Wayland;

    void apply(QWindow *window);
    void release(Blur &blur);
    // Which protocol blurs, or whether any does, changed: every blur is made
    // again with the one that blurs now, or, with none, every blur and
    // region goes until set() asks again.
    void blurChanged();

    std::unique_ptr<Wayland> m_wayland;
    SurfaceOf m_surfaceOf;
    QHash<QWindow *, Blur> m_windows;
};

} // namespace speecher
