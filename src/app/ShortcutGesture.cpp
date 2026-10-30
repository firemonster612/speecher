#include "app/ShortcutGesture.h"

#include "core/SettingsStore.h"

#include <QTimer>

#include <utility>

namespace speecher {
namespace {

// Hybrid: a press toggles, and a hold longer than this is push-to-talk that
// ends with the key. 400 ms suited a two-hand chord; a one-finger tap on a
// single key lands and lifts in about 100 ms, and a deliberate hold is past
// 250 ms before the first word is out, so this sits clear of both gestures.
constexpr qint64 hybridHoldMs = 250;
// Push-to-talk: a press that lifts inside this window is a brush of the key,
// not a dictation, and must leave no trace.
constexpr int pushToTalkMisfireMs = 200;

} // namespace

ShortcutGesture::ShortcutGesture(const SettingsStore &settings, std::function<bool()> sessionActive, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_sessionActive(std::move(sessionActive))
    , m_pushToTalkStart(new QTimer(this))
{
    m_pushToTalkStart->setSingleShot(true);
    m_pushToTalkStart->setInterval(pushToTalkMisfireMs);
    connect(m_pushToTalkStart, &QTimer::timeout, this, &ShortcutGesture::startRequested);
}

// One binding drives every activation mode; the mode decides what a press and
// a release do. The auto-repeat guard below applies to all of them.
void ShortcutGesture::press()
{
    // Key auto-repeat while held must not toggle again. A second press with no
    // release between it and the first is ambiguous: auto-repeat of a held key
    // (ignore) or a deliberate re-press on a desktop that never reports release
    // (toggle). The signals are identical, so the first gesture after launch
    // cannot satisfy both. We bias toward toggle until a release proves the
    // platform reports them: a wrongly swallowed press on a no-release desktop
    // could never be unstuck, whereas the mis-bias only costs the very first
    // hold, and only on a backend that streams auto-repeat as extra presses
    // (KGlobalAccel suppresses those at the binder; today's portal backends
    // fire once). release() latches the guard on for good.
    if (m_down && m_releaseSeen) {
        return;
    }
    // That same unreleased second press is what proves the backend reports no
    // release, and with no release there is no "while held" to honour. From here
    // on push-to-talk takes the toggle path outright, which is the degradation
    // hybrid already gets by never reaching its release branch.
    if (m_down) {
        m_pressedWhileDown = true;
    }
    m_down = true;
    m_press.start();
    m_startedSession = !m_sessionActive();
    if (m_startedSession && reportsRelease()
        && m_settings.dictationSnapshot().shortcutActivationMode == ShortcutActivationMode::PushToTalk) {
        // Deferred rather than started and cancelled: cancelling a Starting
        // session still opens the microphone and shows the popup for an
        // instant, which is a trace. The very first gesture on a desktop that
        // never reports release still lands here; its timer fires and the next
        // press ends the session as in every mode, so nothing wedges.
        m_pushToTalkStart->start();
        return;
    }
    emit toggleRequested();
}

// Everything press() learned applies to the binding and backend it learned it
// on. A new one may report releases where the old one did not, or the
// reverse, and a stale "held, release seen" latch would discard every press
// the new binding makes. No gesture can be in flight across a rebind, so the
// whole gesture state goes back to what it was at launch.
void ShortcutGesture::forget()
{
    m_pushToTalkStart->stop();
    m_down = false;
    m_startedSession = false;
    m_releaseSeen = false;
    m_pressedWhileDown = false;
}

void ShortcutGesture::dropDeferredStart()
{
    m_pushToTalkStart->stop();
}

void ShortcutGesture::forgetStartedSession()
{
    m_startedSession = false;
}

bool ShortcutGesture::reportsRelease() const
{
    return m_releaseSeen || !m_pressedWhileDown;
}

void ShortcutGesture::release(qint64 heldMs)
{
    m_down = false;
    // A release settles the question for good, including against an auto-repeat
    // backend whose extra press looked like a missing one.
    m_releaseSeen = true;
    if (!m_startedSession) {
        return;
    }
    m_startedSession = false;
    const bool starting = m_sessionActive();
    // Whatever the mode is now, a pending deferred start must not survive the
    // release: the mode can have changed since the press, and a session that
    // begins after the key is already up is push-to-talk's misfire, not a tap.
    const bool deferredStartPending = m_pushToTalkStart->isActive();
    m_pushToTalkStart->stop();
    switch (m_settings.dictationSnapshot().shortcutActivationMode) {
    case ShortcutActivationMode::Toggle:
        return;
    case ShortcutActivationMode::PushToTalk:
        if (!deferredStartPending && starting) {
            emit stopRequested();
        }
        return;
    case ShortcutActivationMode::Hybrid: {
        // The first dictation after a launch opens the microphone cold and
        // keeps the main thread busy for a few hundred milliseconds, so a
        // tap's release is handled well past hybridHoldMs. Timed here it read
        // as a hold and cancelled the dictation it had just started; the
        // binder's physical hold says what the key actually did.
        const qint64 held = heldMs >= 0 ? heldMs : m_press.elapsed();
        if (starting && held > hybridHoldMs) {
            emit stopRequested();
        }
        return;
    }
    }
}

} // namespace speecher
