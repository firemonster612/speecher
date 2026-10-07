#include "platform/LinuxSessionKeyGrab.h"

#include "platform/KGlobalAccelShortcutBinder.h"
#include "platform/LinuxDesktopIntegration.h"
#ifdef SPEECHER_WITH_X11
#include "platform/X11KeyGrabShortcutBinder.h"
#endif

#include <algorithm>

namespace speecher {
namespace {

struct TakenKey {
    SessionKeyGrab::Key key;
    QKeySequence keys;
    // Plasma lists it in the desktop's shortcut settings while it is held.
    // Nothing is stored, so it has no settings key.
    GlobalShortcutAction action;
};

QList<TakenKey> takenKeys()
{
    return {
        {SessionKeyGrab::Key::Escape, QKeySequence(Qt::Key_Escape),
         {QString(), QStringLiteral("session-escape"), QStringLiteral("Keep the original while reviewing an edit"), {}, true}},
        {SessionKeyGrab::Key::Enter, QKeySequence(Qt::Key_Return),
         {QString(), QStringLiteral("session-enter"), QStringLiteral("Replace the selection while reviewing an edit"), {}, true}},
        {SessionKeyGrab::Key::Enter, QKeySequence(Qt::KeypadModifier | Qt::Key_Enter),
         {QString(), QStringLiteral("session-keypad-enter"), QStringLiteral("Replace the selection with keypad Enter while reviewing an edit"), {}, true}},
    };
}

SessionShortcutBinder *binderFor(const GlobalShortcutAction &action, QObject *parent)
{
    auto *plasma = new KGlobalAccelSessionShortcutBinder(action, parent);
    if (plasma->supported()) {
        plasma->dropLeftoverRegistration();
        return plasma;
    }
    delete plasma;
#ifdef SPEECHER_WITH_X11
    if (!isWaylandSession()) {
        return new X11KeyGrabShortcutBinder(action, parent);
    }
#endif
    return nullptr;
}

} // namespace

LinuxSessionKeyGrab::LinuxSessionKeyGrab(QObject *parent)
    : SessionKeyGrab(parent)
{
    for (const TakenKey &taken : takenKeys()) {
        SessionShortcutBinder *binder = binderFor(taken.action, this);
        if (!binder) {
            continue;
        }
        binder->holdWhileArmed(taken.keys);
        connect(binder, &GlobalShortcutBinder::activated, this, [this, key = taken.key] { emit pressed(key); });
        m_binders[size_t(taken.key)].append(binder);
    }
}

// Held when any of its physical keys is: a keypad Enter another app owns
// leaves the main Enter working.
bool LinuxSessionKeyGrab::setGrabbed(Key key, bool grabbed)
{
    const QList<SessionShortcutBinder *> &binders = m_binders[size_t(key)];
    for (SessionShortcutBinder *binder : binders) {
        binder->setArmed(grabbed);
    }
    return std::any_of(binders.cbegin(), binders.cend(),
                       [](const SessionShortcutBinder *binder) { return binder->holding(); });
}

} // namespace speecher
