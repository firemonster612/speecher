#include "platform/win/WinCancelKeyGrab.h"

#include <QCoreApplication>
#include <QDebug>

#include <windows.h>

namespace speecher {
namespace {

// Clear of the ids the Global Shortcut binders take from 0x5350 up.
constexpr int escapeHotKeyId = 0x53EC;

} // namespace

WinCancelKeyGrab::WinCancelKeyGrab(QObject *parent)
    : CancelKeyGrab(parent)
{
    QCoreApplication::instance()->installNativeEventFilter(this);
}

WinCancelKeyGrab::~WinCancelKeyGrab()
{
    QCoreApplication::instance()->removeNativeEventFilter(this);
    setGrabbed(false);
}

void WinCancelKeyGrab::setGrabbed(bool grabbed)
{
    if (grabbed == m_grabbed) {
        return;
    }
    if (!grabbed) {
        UnregisterHotKey(nullptr, escapeHotKeyId);
        m_grabbed = false;
        return;
    }
    if (!RegisterHotKey(nullptr, escapeHotKeyId, MOD_NOREPEAT, VK_ESCAPE)) {
        qWarning() << "Could not take Escape to cancel dictation, error" << GetLastError();
        return;
    }
    m_grabbed = true;
}

bool WinCancelKeyGrab::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *)
{
    if (eventType != QByteArrayLiteral("windows_dispatcher_MSG")
        && eventType != QByteArrayLiteral("windows_generic_MSG")) {
        return false;
    }
    const auto *nativeMessage = static_cast<MSG *>(message);
    if (m_grabbed && nativeMessage->message == WM_HOTKEY
        && int(nativeMessage->wParam) == escapeHotKeyId) {
        emit pressed();
    }
    return false;
}

} // namespace speecher
