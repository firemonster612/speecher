#include "platform/mac/MacClickThrough.h"

#include <QCursor>
#include <QGuiApplication>
#include <QRect>
#include <QWindow>

#import <AppKit/AppKit.h>

namespace speecher::mac {
namespace {

const QString followerName = QStringLiteral("speecherClickThrough");

// Follows the pointer while its window is visible, on every move, so the
// window takes or passes on a click from the moment the pointer crosses the
// region's edge. AppKit sends a window that ignores the mouse no events, so
// the moves come from monitors: the global one sees those going to other
// apps, the local one those going to Speecher's own windows, this one too.
class PointerFollower final : public QObject {
public:
    explicit PointerFollower(QWindow *window)
        : QObject(window)
        , m_window(window)
    {
        setObjectName(followerName);
        connect(window, &QWindow::visibleChanged, this, [this](bool visible) {
            visible ? watch() : unwatch();
        });
        if (window->isVisible()) {
            watch();
        }
    }

    ~PointerFollower() override { unwatch(); }

    void setRegion(const QRect &region)
    {
        m_region = region;
        if (m_window->isVisible()) {
            follow();
        }
    }

private:
    void watch()
    {
        // At once: the pointer may already be over the capsule as it appears.
        follow();
        if (m_globalMonitor) {
            return;
        }
        constexpr NSEventMask moves = NSEventMaskMouseMoved | NSEventMaskLeftMouseDragged
            | NSEventMaskRightMouseDragged | NSEventMaskOtherMouseDragged;
        m_globalMonitor = [[NSEvent addGlobalMonitorForEventsMatchingMask:moves
                                                                  handler:^(NSEvent *) { follow(); }] retain];
        m_localMonitor = [[NSEvent addLocalMonitorForEventsMatchingMask:moves
                                                                 handler:^NSEvent *(NSEvent *event) {
                                                                     follow();
                                                                     return event;
                                                                 }] retain];
    }

    void unwatch()
    {
        for (id *monitor : {&m_globalMonitor, &m_localMonitor}) {
            if (*monitor) {
                [NSEvent removeMonitor:*monitor];
                [*monitor release];
                *monitor = nil;
            }
        }
    }

    void follow()
    {
        NSView *view = reinterpret_cast<NSView *>(m_window->winId());
        const bool overRegion = m_region.translated(m_window->position()).contains(QCursor::pos());
        view.window.ignoresMouseEvents = !overRegion;
    }

    QWindow *m_window;
    QRect m_region;
    id m_globalMonitor = nil;
    id m_localMonitor = nil;
};

} // namespace

void takeClicksOnlyIn(QWindow *window, const QRect &region)
{
    // winId() only yields an NSView on the cocoa platform; under the
    // offscreen platform (tests) the handle is a foreign type.
    if (QGuiApplication::platformName() != QLatin1String("cocoa")) {
        return;
    }
    auto *follower = static_cast<PointerFollower *>(
        window->findChild<QObject *>(followerName, Qt::FindDirectChildrenOnly));
    if (!follower) {
        follower = new PointerFollower(window);
    }
    follower->setRegion(region);
}

} // namespace speecher::mac
