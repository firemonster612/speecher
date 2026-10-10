#pragma once

#include "platform/SessionKeyGrab.h"

#include <array>

namespace speecher {

// Each key as a Carbon hot key with no modifier, which macOS consumes
// system-wide while it is registered, like the Global Shortcut's combination.
class MacSessionKeyGrab final : public SessionKeyGrab {
public:
    explicit MacSessionKeyGrab(QObject *parent = nullptr);
    ~MacSessionKeyGrab() override;

    bool setGrabbed(Key key, bool grabbed) override;

private:
    // EventHotKeyRef for each physical key (Escape, Return, keypad Enter),
    // EventHandlerRef and EventHandlerUPP, kept opaque so this header stays
    // plain C++ for moc.
    std::array<void *, 3> m_hotKeys{};
    void *m_eventHandler = nullptr;
    void *m_eventHandlerUpp = nullptr;
};

} // namespace speecher
