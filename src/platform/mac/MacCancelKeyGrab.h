#pragma once

#include "platform/CancelKeyGrab.h"

namespace speecher {

// Escape as a Carbon hot key with no modifier, which macOS consumes
// system-wide while it is registered, like the Global Shortcut's combination.
class MacCancelKeyGrab final : public CancelKeyGrab {
public:
    explicit MacCancelKeyGrab(QObject *parent = nullptr);
    ~MacCancelKeyGrab() override;

    void setGrabbed(bool grabbed) override;

private:
    // EventHotKeyRef, EventHandlerRef and EventHandlerUPP, kept opaque so this
    // header stays plain C++ for moc.
    void *m_hotKey = nullptr;
    void *m_eventHandler = nullptr;
    void *m_eventHandlerUpp = nullptr;
};

} // namespace speecher
