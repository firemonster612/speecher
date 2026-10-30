#include "frontend/quick/host/QuickPopupWindow.h"

#include "frontend/quick/host/PopupWindowBase.h"
#include "platform/PopupPositioner.h"
#ifdef Q_OS_MACOS
#include "platform/mac/MacClickThrough.h"
#endif

#include <QDebug>
#include <QDir>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QRegion>
#include <QTimer>

#ifdef SPEECHER_WITH_X11
// Last: Xlib's macros (None, Status, Bool) would clash with Qt's headers.
#include <X11/Xlib.h>
#include <X11/extensions/shape.h>
#endif

namespace speecher {

namespace {

// About ten frames a second: enough to see every phase of a dictation.
constexpr int captureIntervalMs = 100;

#ifdef SPEECHER_WITH_X11
// Only rect takes the pointer; the window's shape, which also clips what is
// drawn, stays whole.
void setX11InputShape(Display *display, const QWindow &window, const QRect &rect)
{
    const qreal ratio = window.devicePixelRatio();
    const QRect native = QRectF(QPointF(rect.topLeft()) * ratio, QSizeF(rect.size()) * ratio).toAlignedRect();
    XRectangle shape{short(native.x()), short(native.y()), ushort(native.width()), ushort(native.height())};
    XShapeCombineRectangles(display, window.winId(), ShapeInput, 0, 0, &shape, 1, ShapeSet, Unsorted);
    XFlush(display);
}
#endif

} // namespace

QuickPopupWindow::QuickPopupWindow(QQmlEngine *engine, PopupPositioner *positioner, QObject *parent)
    : QObject(parent)
    , m_positioner(positioner)
{
    m_positioner->setParent(this);
    QQmlComponent component(engine, QStringLiteral("Speecher.Popup"), QStringLiteral("PopupWindow"));
    QObject *object = component.create();
    m_window = qobject_cast<PopupWindowBase *>(object);
    if (!m_window) {
        qWarning().noquote() << "QML: could not make the dictation popup:" << component.errorString();
        delete object;
        return;
    }
    // Once, before the first show: a layer-shell surface takes its role then.
    m_positioner->configurePopup(*this);
    // Both on the render thread. A frame answers for the show it was
    // synchronized after, read while the GUI thread waits in the sync, so
    // neither a frame already on its way at a show nor a late answer for an
    // earlier show presents a later one.
    connect(m_window, &QQuickWindow::afterSynchronizing, this, [this] {
        m_syncedGeneration = m_pendingGeneration;
    }, Qt::DirectConnection);
    connect(m_window, &QQuickWindow::frameSwapped, this, [this] {
        QMetaObject::invokeMethod(this, [this, generation = m_syncedGeneration] {
            framePresented(generation);
        }, Qt::QueuedConnection);
    }, Qt::DirectConnection);
#ifdef Q_OS_WIN
    connect(m_window, &PopupWindowBase::shadowRegionChanged, this, &QuickPopupWindow::applyInputRegion);
#else
    connect(m_window, &PopupWindowBase::inputRegionChanged, this, &QuickPopupWindow::applyInputRegion);
#endif
    connect(m_window, &PopupWindowBase::emptyChanged, this, &QuickPopupWindow::hideOnceEmpty);
    // The window is sized before it shows, but a size that still changes
    // while it is up (a review answered by other keys than the configured
    // ones) would keep its top-left corner and move the capsule off centre.
    connect(m_window, &QWindow::widthChanged, this, &QuickPopupWindow::placeAgainWhileShown);
    connect(m_window, &QWindow::heightChanged, this, &QuickPopupWindow::placeAgainWhileShown);
    applyInputRegion();
    captureWhenAsked();
}

QuickPopupWindow::~QuickPopupWindow()
{
    delete m_window;
}

void QuickPopupWindow::show(quint64 generation)
{
    if (!m_window) {
        return;
    }
    m_pendingGeneration = generation;
    m_positioner->positionBottomCenter(*this);
    m_window->show();
    // A popup already up may have nothing new to draw, and this dictation
    // still waits for a frame.
    m_window->update();
}

void QuickPopupWindow::hide()
{
    m_pendingGeneration = 0;
    if (m_window) {
        m_window->hide();
    }
}

void QuickPopupWindow::placeAgainWhileShown()
{
    if (m_window->isVisible()) {
        m_positioner->positionBottomCenter(*this);
    }
}

void QuickPopupWindow::framePresented(quint64 generation)
{
    if (generation == 0 || generation != m_pendingGeneration) {
        return;
    }
    m_pendingGeneration = 0;
    emit presented(generation);
}

// The window is as big as the capsule ever grows; the rest of it is
// transparent and must let clicks through to the app underneath.
void QuickPopupWindow::applyInputRegion()
{
    const QRect region = m_window->inputRegion().toAlignedRect();
#if defined(Q_OS_MACOS)
    // Cocoa's mask clips drawing too, which cut the shadow off.
    mac::takeClicksOnlyIn(m_window, region);
#elif defined(Q_OS_WIN)
    // The mask is the window region, which clips drawing too, so it takes in
    // the shadow. A click on the shadow, a ring 20 pixels wide at the sides,
    // 12 above the capsule and 28 below it, stays with the popup.
    m_window->setMask(QRegion(m_window->shadowRegion().toAlignedRect()));
#else
#ifdef SPEECHER_WITH_X11
    // On X11 the mask would be the window's shape, cutting off the shadow and
    // the capsule wherever it moves past the region, so only the input shape
    // is set. By name: offscreen also answers to QX11Application, with no
    // display.
    if (QGuiApplication::platformName() == QLatin1String("xcb")) {
        setX11InputShape(qGuiApp->nativeInterface<QNativeInterface::QX11Application>()->display(), *m_window, region);
        return;
    }
#endif
    // On Wayland the mask is the surface's input region.
    m_window->setMask(QRegion(region));
#endif
}

void QuickPopupWindow::hideOnceEmpty()
{
    if (m_window->empty()) {
        hide();
    }
}

// A test hook: SPEECHER_POPUP_CAPTURE_DIR=<dir> saves numbered frames of the
// popup while it is up, since a headless compositor lets no other client
// take a screenshot.
void QuickPopupWindow::captureWhenAsked()
{
    const QString dir = qEnvironmentVariable("SPEECHER_POPUP_CAPTURE_DIR");
    if (dir.isEmpty()) {
        return;
    }
    auto *capture = new QTimer(this);
    capture->setInterval(captureIntervalMs);
    connect(capture, &QTimer::timeout, this, [this, dir, frame = 0]() mutable {
        if (!m_window->isVisible()) {
            return;
        }
        const QString path = QDir(dir).filePath(QStringLiteral("popup-%1.png").arg(++frame, 6, 10, QLatin1Char('0')));
        if (!m_window->grabWindow().save(path)) {
            qWarning().noquote() << "Could not save the popup to" << path;
        }
    });
    capture->start();
}

QSize QuickPopupWindow::preferredSize() const
{
    return m_window->size();
}

void QuickPopupWindow::resizeTo(const QSize &size)
{
    m_window->resize(size);
}

void QuickPopupWindow::moveTo(const QPoint &topLeft)
{
    m_window->setPosition(topLeft);
}

QWindow *QuickPopupWindow::nativeWindow()
{
    m_window->create();
    return m_window;
}

} // namespace speecher
