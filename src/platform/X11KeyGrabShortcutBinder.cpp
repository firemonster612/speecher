#include "platform/X11KeyGrabShortcutBinder.h"

#ifdef SPEECHER_WITH_X11

#include "core/settings/SettingsSchema.h"

#include <QSocketNotifier>

// Xlib last: its macros (None, Bool, KeyPress) collide with Qt names.
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

namespace speecher {
namespace {

// The keysym X names a Qt key by, or 0 where this binder has no mapping.
// Latin-1 keys share their codes; the rest are the ones a person is likely to
// pick for cancel or pause. Qt marks the numeric keypad with a modifier, X
// with keysyms of its own.
KeySym keysymFor(Qt::Key key, bool keypad)
{
    if (keypad) {
        if (key >= Qt::Key_0 && key <= Qt::Key_9) {
            return XK_KP_0 + (key - Qt::Key_0);
        }
        switch (key) {
        case Qt::Key_Enter: return XK_KP_Enter;
        case Qt::Key_Plus: return XK_KP_Add;
        case Qt::Key_Minus: return XK_KP_Subtract;
        case Qt::Key_Asterisk: return XK_KP_Multiply;
        case Qt::Key_Slash: return XK_KP_Divide;
        case Qt::Key_Period: return XK_KP_Decimal;
        default: return 0;
        }
    }
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
constexpr unsigned int shortcutModifiers = ShiftMask | ControlMask | Mod1Mask | Mod4Mask;

int grabError = 0;

int recordGrabError(Display *, XErrorEvent *event)
{
    grabError = event->error_code;
    return 0;
}

} // namespace

// One X connection serves every key grab. While a grabbed key is held, the
// server turns its passive grab into an active grab of the whole keyboard
// and sends every key pressed meanwhile to the grabbing connection; sharing
// one connection routes each of those to the binder that holds the key,
// whichever binder's key started the grab. Opened on first use and kept for
// the life of the process.
class X11KeyGrabShortcutBinder::Connection {
public:
    static Connection &instance()
    {
        static Connection connection;
        return connection;
    }

    Display *display = nullptr;
    QList<X11KeyGrabShortcutBinder *> binders;

    // Ends an active grab a held key left behind, so keys pressed while it is
    // still down reach the focused app. The server may have started one whose
    // press Speecher has not read yet, so this never asks whether one is on.
    // The held key's release goes to the app too, so no binder may go on
    // waiting for one.
    void releaseKeyboard()
    {
        for (X11KeyGrabShortcutBinder *binder : std::as_const(binders)) {
            binder->m_down = false;
        }
        XUngrabKeyboard(display, CurrentTime);
    }

    void readEvents()
    {
        while (XPending(display)) {
            XEvent event;
            XNextEvent(display, &event);
            if (event.type != KeyPress && event.type != KeyRelease) {
                continue;
            }
            for (X11KeyGrabShortcutBinder *binder : std::as_const(binders)) {
                binder->keyEvent(event.type == KeyPress, int(event.xkey.keycode), event.xkey.state);
            }
        }
    }

private:
    Connection()
    {
        display = XOpenDisplay(nullptr);
        if (!display) {
            return;
        }
        // Without it a held key repeats as release-press pairs, and each
        // press would cancel or toggle pause again.
        XkbSetDetectableAutoRepeat(display, True, nullptr);
        auto *notifier = new QSocketNotifier(ConnectionNumber(display), QSocketNotifier::Read);
        QObject::connect(notifier, &QSocketNotifier::activated, notifier, [this] { readEvents(); });
    }
};

X11KeyGrabShortcutBinder::X11KeyGrabShortcutBinder(GlobalShortcutAction action, QObject *parent)
    : SessionShortcutBinder(std::move(action), parent)
    , m_display(Connection::instance().display)
{
    Connection::instance().binders.append(this);
}

X11KeyGrabShortcutBinder::~X11KeyGrabShortcutBinder()
{
    letGo();
    Connection::instance().binders.removeOne(this);
}

bool X11KeyGrabShortcutBinder::supported() const
{
    return m_display != nullptr;
}

QString X11KeyGrabShortcutBinder::unsupportedReason() const
{
    return supported() ? QString() : QStringLiteral("Speecher could not reach the X server.");
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

X11KeyGrabShortcutBinder::Grab X11KeyGrabShortcutBinder::grabFor(Display *display,
                                                                const QKeySequence &combination)
{
    if (combination.isEmpty()) {
        return {};
    }
    const Qt::KeyboardModifiers qtModifiers = combination[0].keyboardModifiers();
    const KeySym keysym = keysymFor(combination[0].key(), qtModifiers & Qt::KeypadModifier);
    const int keycode = keysym ? XKeysymToKeycode(display, keysym) : 0;
    unsigned int modifiers = 0;
    if (qtModifiers & Qt::ShiftModifier) modifiers |= ShiftMask;
    if (qtModifiers & Qt::ControlModifier) modifiers |= ControlMask;
    if (qtModifiers & Qt::AltModifier) modifiers |= Mod1Mask;
    if (qtModifiers & Qt::MetaModifier) modifiers |= Mod4Mask;
    return {keycode, modifiers};
}

bool X11KeyGrabShortcutBinder::take(const QKeySequence &combination)
{
    const Grab keys = m_display ? grabFor(m_display, combination) : Grab();
    if (keys.keycode == 0) {
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
    // The round trips read whatever input was waiting into Xlib's queue,
    // which leaves the socket quiet; read those events now, not at the next
    // one to arrive.
    if (XEventsQueued(m_display, QueuedAlready) > 0) {
        QMetaObject::invokeMethod(this, [] { Connection::instance().readEvents(); }, Qt::QueuedConnection);
    }
    if (grabError != 0) {
        letGo();
        return false;
    }
    return true;
}

void X11KeyGrabShortcutBinder::letGo()
{
    if (!m_display || m_held.keycode == 0) {
        return;
    }
    const Window root = DefaultRootWindow(m_display);
    for (const unsigned int lock : lockVariants) {
        XUngrabKey(m_display, m_held.keycode, m_held.modifiers | lock, root);
    }
    m_held = {};
    // Ungrabbing the key leaves an active grab its press started in place.
    Connection::instance().releaseKeyboard();
    XFlush(m_display);
}

void X11KeyGrabShortcutBinder::keyEvent(bool press, int keycode, unsigned int state)
{
    if (m_held.keycode == 0 || keycode != m_held.keycode) {
        return;
    }
    if (!press) {
        m_down = false;
    } else if (!m_down && (state & shortcutModifiers) == m_held.modifiers) {
        m_down = true;
        emit activated();
    }
}

} // namespace speecher

#endif // SPEECHER_WITH_X11
