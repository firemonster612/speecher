#pragma once

#include "platform/SessionKeyGrab.h"

#include <QKeySequence>
#include <QList>

#include <array>

namespace speecher {

class KeyHold;

// Escape and Enter through the desktop's own way of taking a key for a while:
// KGlobalAccel on Plasma, a passive key grab on X11. Elsewhere on Wayland
// there is none, and it takes nothing.
class LinuxSessionKeyGrab final : public SessionKeyGrab {
public:
    explicit LinuxSessionKeyGrab(QObject *parent = nullptr);

    bool setGrabbed(Key key, bool grabbed) override;

private:
    struct Hold {
        KeyHold *keys;
        QKeySequence combination;
        bool held = false;
    };

    // By Key, a hold for each physical key: Enter is the main key and the
    // keypad's. Empty where the desktop cannot take keys.
    std::array<QList<Hold>, 2> m_holds;
    // By Key, whether a Dictation Session wants it taken.
    std::array<bool, 2> m_grabbed{};
};

} // namespace speecher
