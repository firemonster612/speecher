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

// The UI's System singleton: what Speecher knows and does about the computer
// it runs on. The permission and health checks with their fixes, the virtual
// keyboard's setup and typing test, the route a paste takes, the clipboard
// and opening links, and what the computer can do, which the UI's needs read.
// It carries states and step ids; the UI words them (health.js, the pages).
// The virtual keyboard and removing Speecher are Linux's: elsewhere
// hasVirtualKeyboard and canRemove are false and checks has no keyboard row.
class QuickSystem final : public QObject, public HostSingleton<QuickSystem> {
    Q_OBJECT
    QML_NAMED_ELEMENT(System)
    QML_SINGLETON
    Q_PROPERTY(bool accessibility READ accessibility NOTIFY changed)
    Q_PROPERTY(QVariantList checks READ checks NOTIFY changed)
    Q_PROPERTY(QStringList virtualKeyboardActions READ virtualKeyboardActions NOTIFY changed)
    Q_PROPERTY(QString virtualKeyboardTest READ virtualKeyboardTest NOTIFY changed)
    Q_PROPERTY(QStringList pasteRoute READ pasteRoute NOTIFY changed)
    Q_PROPERTY(QString pasteNeed READ pasteNeed NOTIFY changed)
    Q_PROPERTY(bool wordsStayCopied READ wordsStayCopied NOTIFY changed)
    Q_PROPERTY(bool hasVirtualKeyboard READ hasVirtualKeyboard CONSTANT)
    Q_PROPERTY(bool canOpenShortcutSettings READ canOpenShortcutSettings CONSTANT)
    Q_PROPERTY(bool canRemove READ canRemove CONSTANT)
    Q_PROPERTY(bool canLaunchAtLogin READ canLaunchAtLogin CONSTANT)
    Q_PROPERTY(QString computerName READ computerName CONSTANT)

public:
    // virtualKeyboard and removal must outlive this. No fix or keyboard
    // operation starts while removal runs.
    QuickSystem(ApplicationController *controller, VirtualKeyboardSetup *virtualKeyboard, const QuickRemoval *removal,
                QObject *parent = nullptr);

    // Desktop accessibility is on, so Speecher can tell which app it pastes into.
    bool accessibility() const;
    // One entry per health row, {id, state, reason, fix, failures}: id is the
    // row's (microphoneAccess, desktopAccessibility, globalShortcuts,
    // virtualKeyboardHealth); state is ok, attention, off or working; reason
    // says which of the check's cases it is; fix is the action fix(id) runs,
    // or empty where there is nothing to run; the microphone's, openSettings,
    // opens the system's privacy settings. failures are what went wrong in
    // the last thing tried on the check, [{step, error}]: step is enable
    // for accessibility, and setUp, start, type, stop, remove or filesLeft
    // for the keyboard; error is why, in the words of the part that failed,
    // or empty. They stay until the next try.
    QVariantList checks() const;
    // What can be done to a virtual keyboard that is set up: disable, remove.
    QStringList virtualKeyboardActions() const;
    // The typing test that turns the keyboard on: off, ready (waiting for its
    // field), typing, or missed (the text never arrived).
    QString virtualKeyboardTest() const;
    // The steps the default paste takes with Paste with, in the order core's
    // delivery tries them: insert, press, copy (TextDelivery::pasteRoute).
    QStringList pasteRoute() const;
    // What that paste needs of the computer to get past the clipboard:
    // accessibility, keyboard, either, or empty for nothing
    // (TextDelivery::pasteNeed).
    QString pasteNeed() const;
    // Whether dictated words only reach the clipboard, since what the paste
    // needs is missing. Accessibility on for the session counts.
    bool wordsStayCopied() const;
    // Whether this computer has the virtual keyboard: its check, its setup
    // and pasting with it alone (OutputMethod::hasVirtualKeyboard).
    bool hasVirtualKeyboard() const;
    // Whether openShortcutSettings knows this desktop's shortcut settings.
    bool canOpenShortcutSettings() const;
    // Whether Removal can remove Speecher from this computer.
    bool canRemove() const;
    // Whether the platform can start Speecher at login, as the launchAtLogin
    // setting asks: macOS and Windows.
    bool canLaunchAtLogin() const;
    QString computerName() const;

    // Runs the fix the check offers. A fix that fails says why in the
    // check's failures.
    Q_INVOKABLE void fix(const QString &id);
    // fix, for the setup assistant: a virtual keyboard it sets up pastes
    // Automatically.
    Q_INVOKABLE void fixFromSetup(const QString &id);
    Q_INVOKABLE void disableVirtualKeyboard();
    Q_INVOKABLE void removeVirtualKeyboard();
    // Types the test text into whatever has focus, which the caller focused
    // just before; the field reports what it got through
    // virtualKeyboardTestReceived.
    Q_INVOKABLE void runVirtualKeyboardTest();
    Q_INVOKABLE void virtualKeyboardTestReceived(const QString &text);
    Q_INVOKABLE void cancelVirtualKeyboardTest();
    Q_INVOKABLE void copy(const QString &text);
    // Opens a web link or an absolute path with the desktop. Anything else is
    // refused with a warning in the log.
    Q_INVOKABLE void open(const QString &target);
    Q_INVOKABLE void openShortcutSettings();
    Q_INVOKABLE void quit();

signals:
    void changed();

private:
    // The check with this id, as in checks, or an empty map.
    QVariantMap check(const QString &id) const;
    void runFix(const QString &id, VirtualKeyboardSetup::AfterSetUp afterSetUp);
    // Forgets the check's failures as something new is tried on it.
    void clearFailures(const QString &id);

    ApplicationController *m_controller;
    VirtualKeyboardSetup *m_virtualKeyboard;
    const QuickRemoval *m_removal;
    // Each check's failures, by check id, as checks shows them.
    QHash<QString, QVariantList> m_failures;
};

} // namespace speecher
