#pragma once

#include "platform/KeyHold.h"
#include "platform/SessionShortcutBinder.h"

#ifdef SPEECHER_WITH_X11

struct _XDisplay;

namespace speecher {

// Takes keys, a combination or a bare key such as C or Escape, with an X11
// passive grab on the root window. Every hold shares one X connection.
class X11KeyHold final : public KeyHold {
    Q_OBJECT

public:
    explicit X11KeyHold(QObject *parent = nullptr);
    ~X11KeyHold() override;

    // Whether the X server could be reached.
    bool connected() const;
    // Whether X has a key for the combination.
    bool hasKeyFor(const QKeySequence &combination) const;
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

// A session shortcut's keys through an X11KeyHold, on X11 desktops without
// KGlobalAccel.
class X11KeyGrabShortcutBinder final : public SessionShortcutBinder {
    Q_OBJECT

public:
    X11KeyGrabShortcutBinder(SettingsStore *settings, GlobalShortcutAction action, QObject *parent = nullptr);

    bool supported() const override;
    QString unsupportedReason() const override;
    QString unsupportedBindingReason(const ShortcutBinding &binding) const override;

protected:
    bool take(const QKeySequence &keys) override;
    void letGo() override;

private:
    X11KeyHold *m_keys;
};

} // namespace speecher

#endif // SPEECHER_WITH_X11
