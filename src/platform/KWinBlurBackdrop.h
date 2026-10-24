#pragma once

#include "platform/WindowBackdrop.h"

#include <QHash>
#include <QRegion>

#include <memory>

struct org_kde_kwin_blur;
struct wl_compositor;
struct wl_display;
struct wl_surface;

namespace speecher {

// KWin's blur behind a window on Wayland, through the org_kde_kwin_blur
// protocol. Unavailable where the compositor does not offer the protocol
// (other compositors, X11, KWin with its Blur effect off), or where the
// build lacks it; it follows KWin adding and removing it.
class KWinBlurBackdrop final : public WindowBackdrop {
    Q_OBJECT

public:
    // On Qt's own Wayland connection, or none off Wayland.
    explicit KWinBlurBackdrop(QObject *parent = nullptr);
    // On display, whose default queue someone else dispatches, as Qt does
    // its own; compositor makes the blur regions.
    KWinBlurBackdrop(wl_display *display, wl_compositor *compositor, QObject *parent = nullptr);
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
        // The surface the blur was made for, and the blur; null until the
        // window has a surface.
        wl_surface *surface = nullptr;
        org_kde_kwin_blur *blur = nullptr;
    };
    struct Wayland;

    void apply(QWindow *window);
    void release(Blur &blur);
    // KWin took its blur manager away: every blur goes with it.
    void managerRemoved();

    std::unique_ptr<Wayland> m_wayland;
    QHash<QWindow *, Blur> m_windows;
};

} // namespace speecher
