#include "platform/mac/MacClickThrough.h"

#include <QCursor>
#include <QGuiApplication>
#include <QRect>
#include <QTimer>
#include <QWindow>

#import <AppKit/AppKit.h>

namespace speecher::mac {
namespace {

// Fast enough that the capsule takes a click as soon as the pointer is on it.
constexpr int pointerPollMs = 30;
const QString followerName = QStringLiteral("speecherClickThrough");

// Polls the pointer while its window is visible. AppKit sends a window
// that ignores the mouse no events to learn the pointer's place from.
class PointerFollower final : public QObject {
public:
    explicit PointerFollower(QWindow *window)
        : QObject(window)
        , m_window(window)
    {
        setObjectName(followerName);
        m_poll.setInterval(pointerPollMs);
        connect(&m_poll, &QTimer::timeout, this, &PointerFollower::follow);
        connect(window, &QWindow::visibleChanged, this, [this](bool visible) {
            visible ? m_poll.start() : m_poll.stop();
        });
        if (window->isVisible()) {
            m_poll.start();
        }
    }

    QRect region;

private:
    void follow()
    {
        NSView *view = reinterpret_cast<NSView *>(m_window->winId());
        const bool overRegion = region.translated(m_window->position()).contains(QCursor::pos());
        view.window.ignoresMouseEvents = !overRegion;
    }

    QWindow *m_window;
    QTimer m_poll;
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
    follower->region = region;
}

} // namespace speecher::mac
