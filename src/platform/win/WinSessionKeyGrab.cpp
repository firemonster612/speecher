#include "platform/win/WinSessionKeyGrab.h"

#include <QCoreApplication>
#include <QDebug>

#include <windows.h>

namespace speecher {
namespace {

struct HotKey {
    // Clear of the ids the Global Shortcut binders take from 0x5350 up.
    int id;
    UINT virtualKey;
};

HotKey hotKeyFor(SessionKeyGrab::Key key)
{
    switch (key) {
    case SessionKeyGrab::Key::Escape:
        return {0x53EC, VK_ESCAPE};
    case SessionKeyGrab::Key::Enter:
        break;
    }
    return {0x53ED, VK_RETURN};
}

} // namespace

WinSessionKeyGrab::WinSessionKeyGrab(QObject *parent)
    : SessionKeyGrab(parent)
{
    QCoreApplication::instance()->installNativeEventFilter(this);
}

WinSessionKeyGrab::~WinSessionKeyGrab()
{
    QCoreApplication::instance()->removeNativeEventFilter(this);
    setGrabbed(Key::Escape, false);
    setGrabbed(Key::Enter, false);
}

bool WinSessionKeyGrab::setGrabbed(Key key, bool grabbed)
{
    bool &held = m_held[size_t(key)];
    if (grabbed == held) {
        return held;
    }
    const HotKey hotKey = hotKeyFor(key);
    if (!grabbed) {
        UnregisterHotKey(nullptr, hotKey.id);
        held = false;
        return false;
    }
    if (!RegisterHotKey(nullptr, hotKey.id, MOD_NOREPEAT, hotKey.virtualKey)) {
        qWarning() << "Could not take" << key << "for dictation, error" << GetLastError();
        return false;
    }
    held = true;
    return true;
}

bool WinSessionKeyGrab::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *)
{
    if (eventType != QByteArrayLiteral("windows_dispatcher_MSG")
        && eventType != QByteArrayLiteral("windows_generic_MSG")) {
        return false;
    }
    const auto *nativeMessage = static_cast<MSG *>(message);
    if (nativeMessage->message != WM_HOTKEY) {
        return false;
    }
    for (const Key key : {Key::Escape, Key::Enter}) {
        if (m_held[size_t(key)] && int(nativeMessage->wParam) == hotKeyFor(key).id) {
            emit pressed(key);
        }
    }
    return false;
}

} // namespace speecher
