#include "platform/X11KeyGrabShortcutBinder.h"

#ifdef SPEECHER_WITH_X11

#include "core/settings/SettingsSchema.h"
#include "platform/StoredCombination.h"

#include <QSocketNotifier>

// Xlib last: its macros (None, Bool, KeyPress) collide with Qt names.
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

namespace speecher {
namespace {

// The keysym X names a Qt key by, or 0 where this binder has no mapping.
// Latin-1 keys share their codes; the rest are the ones a person is likely to
// pick for cancel or pause.
KeySym keysymFor(Qt::Key key)
{
    if (key >= Qt::Key_Space && key <= Qt::Key_AsciiTilde) {
        return KeySym(key);
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F35) {
        return XK_F1 + (key - Qt::Key_F1);
    }
    switch (key) {
    case Qt::Key_Escape: return XK_Escape;
    case Qt::Key_Tab: return XK_Tab;
    case Qt::Key_Backspace: return XK_BackSpace;
    case Qt::Key_Return: return XK_Return;
    case Qt::Key_Enter: return XK_KP_Enter;
    case Qt::Key_Insert: return XK_Insert;
    case Qt::Key_Delete: return XK_Delete;
    case Qt::Key_Pause: return XK_Pause;
    case Qt::Key_Print: return XK_Print;
    case Qt::Key_Home: return XK_Home;
    case Qt::Key_End: return XK_End;
    case Qt::Key_Left: return XK_Left;
    case Qt::Key_Up: return XK_Up;
    case Qt::Key_Right: return XK_Right;
    case Qt::Key_Down: return XK_Down;
    case Qt::Key_PageUp: return XK_Page_Up;
    case Qt::Key_PageDown: return XK_Page_Down;
    case Qt::Key_Menu: return XK_Menu;
    default: return 0;
    }
}

// A passive grab matches the modifier state exactly, so Caps Lock and Num Lock
// being on would otherwise defeat it: grab every combination of the two.
constexpr unsigned int numLockMask = Mod2Mask;
constexpr unsigned int lockVariants[] = {0, LockMask, numLockMask, LockMask | numLockMask};

int grabError = 0;

int recordGrabError(Display *, XErrorEvent *event)
{
    grabError = event->error_code;
    return 0;
}

} // namespace

X11KeyGrabShortcutBinder::X11KeyGrabShortcutBinder(GlobalShortcutAction action, QObject *parent)
    : GlobalShortcutBinder(std::move(action), parent)
{
    m_display = XOpenDisplay(nullptr);
    if (!m_display) {
        return;
    }
    // Without it a held key repeats as release-press pairs, and each press
    // would cancel or toggle pause again.
    XkbSetDetectableAutoRepeat(m_display, True, nullptr);
    m_notifier = new QSocketNotifier(ConnectionNumber(m_display), QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, [this] { readEvents(); });
}

X11KeyGrabShortcutBinder::~X11KeyGrabShortcutBinder()
{
    if (m_display) {
        XCloseDisplay(m_display);
    }
}

bool X11KeyGrabShortcutBinder::supported() const
{
    return m_display != nullptr;
}

QString X11KeyGrabShortcutBinder::unsupportedReason() const
{
    return supported() ? QString() : QStringLiteral("Speecher could not reach the X server.");
}

void X11KeyGrabShortcutBinder::bind()
{
    m_binding = storedCombination(action().settingsKey);
    const QString error = holdWhileArmed();
    if (!error.isEmpty()) {
        qWarning("Could not grab %s: %s", qPrintable(action().id), qPrintable(error));
    }
    emit bindingChanged();
}

ShortcutBinding X11KeyGrabShortcutBinder::shortcut() const
{
    return m_binding;
}

QString X11KeyGrabShortcutBinder::unsupportedBindingReason(const ShortcutBinding &binding) const
{
    const QString reason = GlobalShortcutBinder::unsupportedBindingReason(binding);
    if (!reason.isEmpty() || binding.isEmpty()) {
        return reason;
    }
    if (!m_display) {
        return unsupportedReason();
    }
    if (grabFor(m_display, binding.combination()).keycode == 0) {
        return QStringLiteral("X11 has no key for %1.").arg(binding.displayText());
    }
    return QString();
}

bool X11KeyGrabShortcutBinder::setShortcut(const ShortcutBinding &shortcut, QString *error)
{
    QString reason = unsupportedBindingReason(shortcut);
    // Grabbing now, armed or not, is what tells another client already holds
    // the keys while the person is still choosing them.
    if (reason.isEmpty() && !shortcut.isEmpty()) {
        ungrab();
        if (!grab(grabFor(m_display, shortcut.combination()))) {
            reason = globalShortcutOwnedElsewhereText(shortcut);
        }
    }
    if (!reason.isEmpty()) {
        holdWhileArmed();
        if (error) {
            *error = reason;
        }
        return false;
    }
    ungrab();
    m_binding = shortcut;
    holdWhileArmed();
    storeCombination(action().settingsKey, m_binding);
    emit bindingChanged();
    return true;
}

bool X11KeyGrabShortcutBinder::removeRegistration(QString *)
{
    ungrab();
    m_binding = {};
    storeCombination(action().settingsKey, m_binding);
    emit bindingChanged();
    return true;
}

void X11KeyGrabShortcutBinder::setArmed(bool armed)
{
    if (armed == m_armed) {
        return;
    }
    m_armed = armed;
    const QString error = holdWhileArmed();
    if (!error.isEmpty()) {
        qWarning("Could not grab %s for this dictation: %s",
                 qPrintable(action().id), qPrintable(error));
    }
}

void X11KeyGrabShortcutBinder::suspend()
{
    m_suspended = true;
    holdWhileArmed();
}

QString X11KeyGrabShortcutBinder::resume()
{
    m_suspended = false;
    return holdWhileArmed();
}

X11KeyGrabShortcutBinder::Grab X11KeyGrabShortcutBinder::grabFor(Display *display,
                                                                const QKeySequence &combination)
{
    if (combination.isEmpty()) {
        return {};
    }
    const KeySym keysym = keysymFor(combination[0].key());
    const int keycode = keysym ? XKeysymToKeycode(display, keysym) : 0;
    const Qt::KeyboardModifiers qtModifiers = combination[0].keyboardModifiers();
    unsigned int modifiers = 0;
    if (qtModifiers & Qt::ShiftModifier) modifiers |= ShiftMask;
    if (qtModifiers & Qt::ControlModifier) modifiers |= ControlMask;
    if (qtModifiers & Qt::AltModifier) modifiers |= Mod1Mask;
    if (qtModifiers & Qt::MetaModifier) modifiers |= Mod4Mask;
    return {keycode, modifiers};
}

QString X11KeyGrabShortcutBinder::holdWhileArmed()
{
    if (!m_armed || m_suspended || m_binding.isEmpty() || !m_display) {
        ungrab();
        return {};
    }
    if (m_held.keycode != 0) {
        return {};
    }
    return grab(grabFor(m_display, m_binding.combination()))
        ? QString()
        : globalShortcutOwnedElsewhereText(m_binding);
}

bool X11KeyGrabShortcutBinder::grab(const Grab &keys)
{
    if (!m_display || keys.keycode == 0) {
        return false;
    }
    // X reports a key another client already grabbed as an asynchronous
    // BadAccess; the round trip collects it before the handler goes back.
    const Window root = DefaultRootWindow(m_display);
    XSync(m_display, False);
    grabError = 0;
    const auto previous = XSetErrorHandler(recordGrabError);
    for (const unsigned int lock : lockVariants) {
        XGrabKey(m_display, keys.keycode, keys.modifiers | lock, root, False,
                 GrabModeAsync, GrabModeAsync);
    }
    XSync(m_display, False);
    XSetErrorHandler(previous);
    m_held = keys;
    m_down = false;
    if (grabError != 0) {
        ungrab();
        return false;
    }
    return true;
}

void X11KeyGrabShortcutBinder::ungrab()
{
    if (!m_display || m_held.keycode == 0) {
        return;
    }
    const Window root = DefaultRootWindow(m_display);
    for (const unsigned int lock : lockVariants) {
        XUngrabKey(m_display, m_held.keycode, m_held.modifiers | lock, root);
    }
    XFlush(m_display);
    m_held = {};
}

void X11KeyGrabShortcutBinder::readEvents()
{
    while (XPending(m_display)) {
        XEvent event;
        XNextEvent(m_display, &event);
        if (m_held.keycode == 0 || int(event.xkey.keycode) != m_held.keycode) {
            continue;
        }
        if (event.type == KeyPress && !m_down) {
            m_down = true;
            emit activated();
        } else if (event.type == KeyRelease) {
            m_down = false;
        }
    }
}

} // namespace speecher

#endif // SPEECHER_WITH_X11
