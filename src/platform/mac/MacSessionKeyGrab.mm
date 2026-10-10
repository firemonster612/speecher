#include "platform/mac/MacSessionKeyGrab.h"

#include <QDebug>

#include <iterator>

#import <Carbon/Carbon.h>

namespace speecher {
namespace {

// Its own signature, so the Global Shortcut binders' handlers pass it on.
constexpr UInt32 keySignature = 'spce';

// Each physical key a Key takes: Enter is the main key and the keypad's. The
// hot key's identifier is its index here plus one.
struct PhysicalKey {
    SessionKeyGrab::Key key;
    UInt32 keyCode;
};
constexpr PhysicalKey physicalKeys[] = {
    {SessionKeyGrab::Key::Escape, kVK_Escape},
    {SessionKeyGrab::Key::Enter, kVK_Return},
    {SessionKeyGrab::Key::Enter, kVK_ANSI_KeypadEnter},
};

OSStatus handleKey(EventHandlerCallRef, EventRef event, void *userData)
{
    EventHotKeyID pressed;
    if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr,
                          sizeof(pressed), nullptr, &pressed)
            != noErr
        || pressed.signature != keySignature || pressed.id < 1
        || pressed.id > std::size(physicalKeys)) {
        return eventNotHandledErr;
    }
    emit static_cast<MacSessionKeyGrab *>(userData)->pressed(physicalKeys[pressed.id - 1].key);
    return noErr;
}

} // namespace

MacSessionKeyGrab::MacSessionKeyGrab(QObject *parent)
    : SessionKeyGrab(parent)
{
    static const EventTypeSpec pressedEvent{kEventClassKeyboard, kEventHotKeyPressed};
    EventHandlerUPP upp = NewEventHandlerUPP(handleKey);
    EventHandlerRef handler = nullptr;
    if (InstallApplicationEventHandler(upp, 1, &pressedEvent, this, &handler) != noErr) {
        DisposeEventHandlerUPP(upp);
        qWarning() << "Could not install the session key handler";
        return;
    }
    m_eventHandler = handler;
    m_eventHandlerUpp = reinterpret_cast<void *>(upp);
}

MacSessionKeyGrab::~MacSessionKeyGrab()
{
    setGrabbed(Key::Escape, false);
    setGrabbed(Key::Enter, false);
    if (m_eventHandler) {
        RemoveEventHandler(static_cast<EventHandlerRef>(m_eventHandler));
    }
    if (m_eventHandlerUpp) {
        DisposeEventHandlerUPP(reinterpret_cast<EventHandlerUPP>(m_eventHandlerUpp));
    }
}

// Held when any of its physical keys is: a keypad Enter another app owns
// leaves Return working.
bool MacSessionKeyGrab::setGrabbed(Key key, bool grabbed)
{
    bool held = false;
    for (size_t index = 0; index < std::size(physicalKeys); ++index) {
        if (physicalKeys[index].key != key) {
            continue;
        }
        void *&hotKey = m_hotKeys[index];
        if (!grabbed) {
            if (hotKey) {
                UnregisterEventHotKey(static_cast<EventHotKeyRef>(hotKey));
                hotKey = nullptr;
            }
            continue;
        }
        if (!hotKey && m_eventHandler) {
            EventHotKeyRef registered = nullptr;
            const EventHotKeyID identifier{keySignature, UInt32(index + 1)};
            // Exclusive, so a key another app holds is refused rather than
            // shared, and the review never names a key that would reach it too.
            if (RegisterEventHotKey(physicalKeys[index].keyCode, 0, identifier, GetApplicationEventTarget(),
                                    kEventHotKeyExclusive, &registered)
                == noErr) {
                hotKey = registered;
            } else {
                qWarning() << "Could not take" << key << "for dictation";
            }
        }
        held = held || hotKey;
    }
    return held;
}

} // namespace speecher
