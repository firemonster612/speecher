#pragma once

#include "platform/GlobalShortcutBinder.h"

#ifdef SPEECHER_WITH_X11

struct _XDisplay;
class QSocketNotifier;

namespace speecher {

// Takes a key combination, or a bare key such as C or Escape, with an X11
// passive grab on the root window, for the Cancel and Pause Shortcuts on X11
// desktops without KGlobalAccel. The grab is held only while armed, so the
// focused app gets the key the rest of the time. The binding lives in
// Speecher's settings.
class X11KeyGrabShortcutBinder final : public GlobalShortcutBinder {
    Q_OBJECT

public:
    explicit X11KeyGrabShortcutBinder(GlobalShortcutAction action, QObject *parent = nullptr);
    ~X11KeyGrabShortcutBinder() override;

    bool supported() const override;
    QString unsupportedReason() const override;
    void bind() override;
    ShortcutBinding shortcut() const override;
    QString unsupportedBindingReason(const ShortcutBinding &binding) const override;
    bool setShortcut(const ShortcutBinding &shortcut, QString *error = nullptr) override;
    bool removeRegistration(QString *error = nullptr) override;
    void setArmed(bool armed) override;
    void suspend() override;
    QString resume() override;

private:
    struct Grab {
        int keycode = 0;
        unsigned int modifiers = 0;
    };
    static Grab grabFor(_XDisplay *display, const QKeySequence &combination);
    // Grabs the binding while armed and not suspended, otherwise ungrabs.
    // Returns why the grab failed, such as another client holding the keys.
    QString holdWhileArmed();
    // False when another client already holds the keys.
    bool grab(const Grab &keys);
    void ungrab();
    void readEvents();

    _XDisplay *m_display = nullptr;
    QSocketNotifier *m_notifier = nullptr;
    ShortcutBinding m_binding;
    Grab m_held;
    bool m_armed = false;
    bool m_suspended = false;
    // A held key acts once, on its first press.
    bool m_down = false;
};

} // namespace speecher

#endif // SPEECHER_WITH_X11
