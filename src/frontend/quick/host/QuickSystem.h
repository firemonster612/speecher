#pragma once

#include "app/VirtualKeyboardSetup.h"
#include "frontend/quick/host/HostSingleton.h"

#include <QHash>
#include <QObject>
#include <QQmlEngine>
#include <QVariantList>
#include <QVariantMap>

namespace speecher {

class ApplicationController;
class QuickRemoval;
class QuickSettings;
struct DeliverySteps;

// The UI's System singleton: what Speecher knows and does about the computer
// it runs on. The permission and health checks with their fixes, the virtual
// keyboard's setup and typing test, the delivery steps and what each needs, the clipboard
// and opening links, and what the computer can do, which the UI's needs read.
// It carries states and step ids; the UI words them (health.js, the pages).
// The delivery steps are the settings as Settings shows them, so a draft
// the setup assistant holds shows in the outcomes as in the rows.
// The virtual keyboard and removing Speecher are Linux's: elsewhere
// hasVirtualKeyboard and canRemove are false and checks has no keyboard row.
class QuickSystem final : public QObject, public HostSingleton<QuickSystem> {
    Q_OBJECT
    QML_NAMED_ELEMENT(System)
    QML_SINGLETON
    Q_PROPERTY(bool accessibility READ accessibility NOTIFY changed)
    Q_PROPERTY(QVariantList checks READ checks NOTIFY changed)
    Q_PROPERTY(bool canDisablePermanentAccessibility READ canDisablePermanentAccessibility NOTIFY changed)
    Q_PROPERTY(QStringList virtualKeyboardActions READ virtualKeyboardActions NOTIFY changed)
    Q_PROPERTY(QString virtualKeyboardTest READ virtualKeyboardTest NOTIFY changed)
    Q_PROPERTY(QStringList pasteRoute READ pasteRoute NOTIFY changed)
    Q_PROPERTY(QVariantList deliverySteps READ deliverySteps NOTIFY changed)
    Q_PROPERTY(QString pastingOutcome READ pastingOutcome NOTIFY changed)
    Q_PROPERTY(bool copiesFormatted READ copiesFormatted CONSTANT)
    Q_PROPERTY(bool waylandSession READ waylandSession CONSTANT)
    Q_PROPERTY(bool identifiesAppsWithoutAccessibility READ identifiesAppsWithoutAccessibility CONSTANT)
    Q_PROPERTY(bool hasVirtualKeyboard READ hasVirtualKeyboard CONSTANT)
    Q_PROPERTY(bool hasGlobalShortcuts READ hasGlobalShortcuts NOTIFY changed)
    Q_PROPERTY(bool canOpenShortcutSettings READ canOpenShortcutSettings CONSTANT)
    Q_PROPERTY(bool canRemove READ canRemove CONSTANT)
    Q_PROPERTY(bool canLaunchAtLogin READ canLaunchAtLogin CONSTANT)
    Q_PROPERTY(QString computerName READ computerName CONSTANT)
    Q_PROPERTY(QString desktop READ desktop CONSTANT)

public:
    // settings, virtualKeyboard and removal must outlive this. No fix or
    // keyboard operation starts while removal runs.
    QuickSystem(ApplicationController *controller, const QuickSettings *settings, VirtualKeyboardSetup *virtualKeyboard,
                const QuickRemoval *removal, QObject *parent = nullptr);

    // Desktop accessibility is on, so Speecher can tell which app it pastes into.
    bool accessibility() const;
    // One entry per health row, {id, state, reason, fix, needed, failures}:
    // id is the row's (microphoneAccess, desktopAccessibility,
    // globalShortcuts, virtualKeyboardHealth, and screenRecording on macOS
    // while screenshot context is on); state is ok, attention, off or
    // working; reason says which of the check's cases it is (keptOn for
    // accessibility Speecher keeps on past sign-out), notNeeded for
    // one this computer has nothing to ask for (accessibility on Windows);
    // needed says dictation can't start while it fails, as microphone access
    // can't; fix is the action fix(id) runs,
    // or empty where there is nothing to run; the microphone's and screen
    // recording's, openSettings, open the system's privacy settings. failures are what went wrong in
    // the last thing tried on the check, [{step, error}]: step is enable or
    // disable for accessibility, and setUp, start, type, stop, remove or filesLeft
    // for the keyboard; error is why, in the words of the part that failed,
    // or empty. They stay until the next try.
    QVariantList checks() const;
    // Whether something Speecher set up to keep accessibility on past sign-out
    // is left for disablePermanentAccessibility to undo: Linux only.
    bool canDisablePermanentAccessibility() const;
    // What can be done to a virtual keyboard that is set up: disable, remove.
    QStringList virtualKeyboardActions() const;
    // The typing test that turns the keyboard on: off, ready (waiting for its
    // field), typing, or missed (the text never arrived).
    QString virtualKeyboardTest() const;
    // The delivery steps that are on, in the order delivery tries them:
    // insert, press, keep (DeliveryPlan::route).
    QStringList pasteRoute() const;
    // Every delivery step in that order, {id, on, ready, needs, waiting}:
    // needs are what the step relies on here, by need id, and waiting those
    // of them this computer lacks now, so ready is waiting being empty.
    // The need ids: accessibility (Insert, and Press on macOS; Windows needs
    // none), virtualKeyboard (Press on Linux), clipboard (in a Wayland
    // session Speecher's clipboard helper or wl-copy, for Press and for Keep
    // on) and clipboardSave (wl-paste there, for Keep off and for Press
    // while Keep is off, which saves the clipboard it borrows).
    QVariantList deliverySteps() const;
    // Where dictated words end up as the steps stand: delivers (a step
    // that is on gets them into the app), clipboard (none can, and Keep
    // leaves them there) or nothing.
    QString pastingOutcome() const;
    // What delivering with these steps ({ insert, press, keep }, an App's
    // own or the Pasting page's) does here: {route, usable}, route the steps
    // that are on in order (DeliveryPlan::route), usable those of them this
    // computer can run now.
    Q_INVOKABLE QVariantMap stepsPlan(const QVariantMap &steps) const;
    // Whether a paste can carry formatted text here: not in a Wayland
    // session without Speecher's clipboard helper, where wl-copy copies
    // plain text only.
    bool copiesFormatted() const;
    // Whether this is a Wayland session, whose clipboard takes helpers.
    bool waylandSession() const;
    // Whether Speecher can tell which app is in front without accessibility,
    // if not always how to treat it: from the system on macOS and Windows,
    // from KWin on Plasma. Elsewhere on Linux it has no way to.
    bool identifiesAppsWithoutAccessibility() const;
    // Whether this computer has the virtual keyboard: its check and its
    // setup, under Press (PasteKeys::hasVirtualKeyboard).
    bool hasVirtualKeyboard() const;
    // Whether the desktop gives Speecher global shortcuts, or may: true until
    // the shortcut service has answered.
    bool hasGlobalShortcuts() const;
    // Whether openShortcutSettings knows this desktop's shortcut settings.
    bool canOpenShortcutSettings() const;
    // Whether Removal can remove Speecher from this computer.
    bool canRemove() const;
    // Whether the platform can start Speecher at login, as the launchAtLogin
    // setting asks: macOS and Windows.
    bool canLaunchAtLogin() const;
    QString computerName() const;
    // The desktop on Linux, lower case, as XDG_CURRENT_DESKTOP names it first
    // (gnome, kde, sway, hyprland); empty elsewhere.
    QString desktop() const;

    // Runs the fix the check offers. A fix that fails says why in the
    // check's failures.
    Q_INVOKABLE void fix(const QString &id);
    // fix, for the setup assistant. The same as fix since setup chooses the
    // delivery steps itself.
    Q_INVOKABLE void fixFromSetup(const QString &id);
    // Stops keeping accessibility on past sign-out, and Chromium and Electron
    // apps building their accessibility tree; it stays on for this session.
    Q_INVOKABLE void disablePermanentAccessibility();
    Q_INVOKABLE void disableVirtualKeyboard();
    Q_INVOKABLE void removeVirtualKeyboard();
    // Types the test text into whatever has focus, which the caller focused
    // just before; the field reports what it got through
    // virtualKeyboardTestReceived.
    Q_INVOKABLE void runVirtualKeyboardTest();
    Q_INVOKABLE void virtualKeyboardTestReceived(const QString &text);
    Q_INVOKABLE void cancelVirtualKeyboardTest();
    Q_INVOKABLE void copy(const QString &text);
    // Opens a web link (http or https, with a host) in the browser. Anything
    // else, a path or another scheme, is refused with a warning in the log.
    Q_INVOKABLE void open(const QString &target);
    Q_INVOKABLE void openShortcutSettings();
    Q_INVOKABLE void quit();

signals:
    void changed();

private:
    // The check with this id, as in checks, or an empty map.
    QVariantMap check(const QString &id) const;
    // deliverySteps for these switches.
    QVariantList stepEntries(const DeliverySteps &steps) const;
    // Forgets the check's failures as something new is tried on it.
    void clearFailures(const QString &id);

    ApplicationController *m_controller;
    const QuickSettings *m_settings;
    VirtualKeyboardSetup *m_virtualKeyboard;
    const QuickRemoval *m_removal;
    // Each check's failures, by check id, as checks shows them.
    QHash<QString, QVariantList> m_failures;
};

} // namespace speecher
