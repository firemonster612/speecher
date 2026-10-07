#pragma once

#include "platform/SessionKeyGrab.h"

#include <QList>

#include <array>

namespace speecher {

class SessionShortcutBinder;

// Escape and Enter through the desktop's own way of taking a key for a while:
// KGlobalAccel on Plasma, a passive key grab on X11. Elsewhere on Wayland
// there is none, and it takes nothing.
class LinuxSessionKeyGrab final : public SessionKeyGrab {
public:
    explicit LinuxSessionKeyGrab(QObject *parent = nullptr);

    bool setGrabbed(Key key, bool grabbed) override;

private:
    // By Key, a binder for each physical key: Enter is the main key and the
    // keypad's. Empty where the desktop cannot take keys.
    std::array<QList<SessionShortcutBinder *>, 2> m_binders;
};

} // namespace speecher
