#include "platform/mac/MacCancelKeyGrab.h"

#include <QDebug>

#import <Carbon/Carbon.h>

namespace speecher {
namespace {

// Its own signature, so the Global Shortcut binders' handlers pass it on.
constexpr UInt32 escapeSignature = 'spce';
constexpr UInt32 escapeIdentifier = 1;

OSStatus handleEscape(EventHandlerCallRef, EventRef event, void *userData)
{
    EventHotKeyID pressed;
    if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr,
                          sizeof(pressed), nullptr, &pressed)
            != noErr
        || pressed.signature != escapeSignature || pressed.id != escapeIdentifier) {
        return eventNotHandledErr;
    }
    emit static_cast<MacCancelKeyGrab *>(userData)->pressed();
    return noErr;
}

} // namespace

MacCancelKeyGrab::MacCancelKeyGrab(QObject *parent)
    : CancelKeyGrab(parent)
{
    static const EventTypeSpec pressedEvent{kEventClassKeyboard, kEventHotKeyPressed};
    EventHandlerUPP upp = NewEventHandlerUPP(handleEscape);
    EventHandlerRef handler = nullptr;
    if (InstallApplicationEventHandler(upp, 1, &pressedEvent, this, &handler) != noErr) {
        DisposeEventHandlerUPP(upp);
        qWarning() << "Could not install the Escape hot-key handler";
        return;
    }
    m_eventHandler = handler;
    m_eventHandlerUpp = reinterpret_cast<void *>(upp);
}

MacCancelKeyGrab::~MacCancelKeyGrab()
{
    setGrabbed(false);
    if (m_eventHandler) {
        RemoveEventHandler(static_cast<EventHandlerRef>(m_eventHandler));
    }
    if (m_eventHandlerUpp) {
        DisposeEventHandlerUPP(reinterpret_cast<EventHandlerUPP>(m_eventHandlerUpp));
    }
}

void MacCancelKeyGrab::setGrabbed(bool grabbed)
{
    if (grabbed == (m_hotKey != nullptr)) {
        return;
    }
    if (!grabbed) {
        UnregisterEventHotKey(static_cast<EventHotKeyRef>(m_hotKey));
        m_hotKey = nullptr;
        return;
    }
    if (!m_eventHandler) {
        return;
    }
    EventHotKeyRef hotKey = nullptr;
    const EventHotKeyID identifier{escapeSignature, escapeIdentifier};
    if (RegisterEventHotKey(kVK_Escape, 0, identifier, GetApplicationEventTarget(), 0, &hotKey)
        != noErr) {
        qWarning() << "Could not take Escape to cancel dictation";
        return;
    }
    m_hotKey = hotKey;
}

} // namespace speecher
