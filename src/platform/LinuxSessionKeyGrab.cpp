#include "platform/LinuxSessionKeyGrab.h"

#include "platform/KGlobalAccelShortcutBinder.h"
#include "platform/LinuxDesktopIntegration.h"
#ifdef SPEECHER_WITH_X11
#include "platform/X11KeyGrabShortcutBinder.h"
#endif

#include <algorithm>
#include <utility>

namespace speecher {
namespace {

struct TakenKey {
    SessionKeyGrab::Key key;
    QKeySequence keys;
    // Plasma lists it in the desktop's shortcut settings while it is held.
    QString id;
    QString description;
};

QList<TakenKey> takenKeys()
{
    return {
        {SessionKeyGrab::Key::Escape, QKeySequence(Qt::Key_Escape), QStringLiteral("session-escape"),
         QStringLiteral("Keep the original while reviewing an edit")},
        {SessionKeyGrab::Key::Enter, QKeySequence(Qt::Key_Return), QStringLiteral("session-enter"),
         QStringLiteral("Replace the selection while reviewing an edit")},
        {SessionKeyGrab::Key::Enter, QKeySequence(Qt::KeypadModifier | Qt::Key_Enter),
         QStringLiteral("session-keypad-enter"),
         QStringLiteral("Replace the selection with keypad Enter while reviewing an edit")},
    };
}

KeyHold *keyHoldFor(const TakenKey &taken, QObject *parent)
{
    if (KGlobalAccelKeyHold::available()) {
        auto *plasma = new KGlobalAccelKeyHold(taken.id, taken.description, parent);
        plasma->dropLeftoverRegistration();
        return plasma;
    }
#ifdef SPEECHER_WITH_X11
    if (!isWaylandSession()) {
        return new X11KeyHold(parent);
    }
#endif
    return nullptr;
}

} // namespace

LinuxSessionKeyGrab::LinuxSessionKeyGrab(QObject *parent)
    : SessionKeyGrab(parent)
{
    for (const TakenKey &taken : takenKeys()) {
        KeyHold *keys = keyHoldFor(taken, this);
        if (!keys) {
            continue;
        }
        connect(keys, &KeyHold::pressed, this, [this, key = taken.key] { emit pressed(key); });
        m_holds[size_t(taken.key)].append({keys, taken.keys});
    }
}

// Held when any of its physical keys is: a keypad Enter another app owns
// leaves the main Enter working.
bool LinuxSessionKeyGrab::setGrabbed(Key key, bool grabbed)
{
    QList<Hold> &holds = m_holds[size_t(key)];
    if (std::exchange(m_grabbed[size_t(key)], grabbed) != grabbed) {
        for (Hold &hold : holds) {
            hold.keys->letGo();
            hold.held = grabbed && hold.keys->take(hold.combination);
        }
    }
    return std::any_of(holds.cbegin(), holds.cend(), [](const Hold &hold) { return hold.held; });
}

} // namespace speecher
