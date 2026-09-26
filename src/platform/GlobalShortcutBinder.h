#pragma once

#include "core/ShortcutBinding.h"

#include <QObject>
#include <QString>

namespace speecher {

// Binds one desktop-wide ShortcutBinding to dictation. Platforms that report
// key release drive push-to-talk through activated()/deactivated(); platforms
// that only report a trigger emit activated() alone.
class GlobalShortcutBinder : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    static QKeySequence defaultShortcut()
    {
        return QKeySequence(Qt::META | Qt::ALT | Qt::Key_D);
    }

    virtual bool supported() const = 0;
    virtual bool supportKnown() const { return true; }
    virtual bool usesDesktopShortcutChooser() const { return false; }
    // Empty while supported(); otherwise what to tell the user.
    virtual QString unsupportedReason() const = 0;
    // Registers the binding with the desktop shortcut service. Kept out of the
    // constructor because registration costs a round trip to that service.
    virtual void bind() = 0;
    virtual ShortcutBinding shortcut() const = 0;
    virtual QString shortcutDisplay() const
    {
        return shortcut().displayText();
    }
    // Empty when this binder can honour the binding; otherwise what to tell
    // the user. Answered per binding, not per binder: a backend may take any
    // combination yet only some single keys, as the Wayland helper allows only
    // keys that cannot spell text. setShortcut() refuses whatever is reported
    // here. Desktop shortcut services take combinations only, so the default
    // turns every single key away until a backend that watches the key itself
    // says otherwise.
    virtual QString unsupportedBindingReason(const ShortcutBinding &binding) const
    {
        return binding.isSingleKey()
            ? QStringLiteral("Global Shortcuts on this desktop need a key combination, not a single key.")
            : QString();
    }
    virtual bool setShortcut(const ShortcutBinding &shortcut, QString *error = nullptr) = 0;
    // Recording a replacement needs the current combination delivered as an
    // ordinary key event. A platform that consumes it system-wide lets go of
    // the registration here and takes it back on resume; the default binders
    // deliver key events regardless and keep nothing to let go of.
    virtual void suspend() {}
    virtual QString resume() { return {}; }
    // Forgets the registration the desktop keeps for Speecher, where the
    // desktop keeps one. Portal shortcuts live with the session and need no
    // removal; the default says so.
    virtual bool removeRegistration(QString *error = nullptr)
    {
        if (error) {
            *error = QStringLiteral("Your desktop keeps no shortcut registration to remove.");
        }
        return false;
    }
    virtual void registerShortcut()
    {
        if (!supported()) {
            emit registrationFinished(false, unsupportedReason());
            return;
        }
        bind();
        const QString display = shortcutDisplay();
        emit registrationFinished(
            !display.isEmpty(),
            display.isEmpty()
                ? QStringLiteral("Your desktop didn't say which keys it assigned.")
                : display);
    }

signals:
    void activated();
    // heldMs is how long the key was physically down, read from the events'
    // own timestamps, or -1 where the backend cannot tell. The main thread can
    // be busy for hundreds of milliseconds (a cold microphone open), and a tap
    // whose release is only dispatched afterwards must not read as a hold.
    void deactivated(qint64 heldMs = -1);
    void bindingChanged();
    void supportChanged();
    void registrationFinished(bool bound, const QString &detail);
};

} // namespace speecher
