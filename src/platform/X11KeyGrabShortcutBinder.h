#pragma once

#include "platform/SessionShortcutBinder.h"

#ifdef SPEECHER_WITH_X11

struct _XDisplay;

namespace speecher {

// Takes a session shortcut's keys, a combination or a bare key such as C or
// Escape, with an X11 passive grab on the root window, on X11 desktops without
// KGlobalAccel. Every binder shares one X connection.
class X11KeyGrabShortcutBinder final : public SessionShortcutBinder {
    Q_OBJECT

public:
    explicit X11KeyGrabShortcutBinder(GlobalShortcutAction action, QObject *parent = nullptr);
    ~X11KeyGrabShortcutBinder() override;

    bool supported() const override;
    QString unsupportedReason() const override;
    QString unsupportedBindingReason(const ShortcutBinding &binding) const override;

protected:
    bool take(const QKeySequence &keys) override;
    void letGo() override;

private:
    struct Grab {
        int keycode = 0;
        unsigned int modifiers = 0;
    };
    static Grab grabFor(_XDisplay *display, const QKeySequence &combination);
    void keyEvent(bool press, int keycode, unsigned int state);

    class Connection;
    _XDisplay *m_display = nullptr;
    Grab m_held;
    // A held key acts once, on its first press.
    bool m_down = false;
};

} // namespace speecher

#endif // SPEECHER_WITH_X11
