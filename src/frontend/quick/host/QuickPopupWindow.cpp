#include "frontend/quick/host/QuickPopupWindow.h"

#include "frontend/quick/host/PopupWindowBase.h"
#include "platform/PopupInputRegion.h"
#include "platform/PopupPositioner.h"

#include <QDebug>
#include <QDir>
#include <QQmlComponent>
#include <QTimer>

namespace speecher {

namespace {

// About ten frames a second: enough to see every phase of a dictation.
constexpr int captureIntervalMs = 100;

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
    connect(m_window, &PopupWindowBase::inputRegionChanged, this, &QuickPopupWindow::applyInputRegion);
    connect(m_window, &PopupWindowBase::shadowRegionChanged, this, &QuickPopupWindow::applyInputRegion);
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
    setPopupInputRegion(*m_window, m_window->inputRegion().toAlignedRect(), m_window->shadowRegion().toAlignedRect());
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
