#pragma once

#include "app/SpeecherRemoval.h"
#include "app/VirtualKeyboardSetup.h"

#include <QHash>
#include <QObject>
#include <QQmlEngine>
#include <QVariantList>
#include <QVariantMap>

#include <memory>
#include <optional>

namespace speecher {

class ApplicationController;
class PhoneTransfer;

// The UI's System singleton: what Speecher knows and does about the computer
// it runs on. The permission and health checks with their fixes, the virtual
// keyboard's setup and typing test, the route a paste takes, the clipboard
// and opening links, sending settings to the phone, and removing Speecher.
// It carries states and step ids; the UI words them (health.js, the pages).
// The virtual keyboard and removing Speecher are Linux's: elsewhere checks
// has no keyboard row and canRemove is false.
class QuickSystem final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(System)
    QML_SINGLETON
    Q_PROPERTY(bool accessibility READ accessibility NOTIFY changed)
    Q_PROPERTY(QVariantList checks READ checks NOTIFY changed)
    Q_PROPERTY(QStringList virtualKeyboardActions READ virtualKeyboardActions NOTIFY changed)
    Q_PROPERTY(QString virtualKeyboardTest READ virtualKeyboardTest NOTIFY changed)
    Q_PROPERTY(QStringList pasteRoute READ pasteRoute NOTIFY changed)
    Q_PROPERTY(bool canOpenShortcutSettings READ canOpenShortcutSettings CONSTANT)
    Q_PROPERTY(bool canRemove READ canRemove CONSTANT)
    Q_PROPERTY(QString computerName READ computerName CONSTANT)
    Q_PROPERTY(QString phoneTransfer READ phoneTransfer NOTIFY phoneChanged)
    Q_PROPERTY(QString phoneLink READ phoneLink NOTIFY phoneChanged)
    Q_PROPERTY(QVariantMap phoneBundle READ phoneBundle NOTIFY phoneChanged)
    Q_PROPERTY(bool removing READ removing NOTIFY removalChanged)
    Q_PROPERTY(QVariantMap removal READ removal NOTIFY removalChanged)

public:
    // The defaults touch the real computer; tests pass stand-ins.
    explicit QuickSystem(ApplicationController *controller, QObject *parent = nullptr);
#ifdef Q_OS_LINUX
    QuickSystem(ApplicationController *controller, VirtualKeyboardSetup::System virtualKeyboard,
                SpeecherRemoval::Steps removal, QObject *parent = nullptr);
#endif
    ~QuickSystem() override;

    // The engine's instance: the one the host made before loading any QML.
    static QuickSystem *create(QQmlEngine *, QJSEngine *);

    // Desktop accessibility is on, so Speecher can tell which app it pastes into.
    bool accessibility() const;
    // One entry per health row, {id, state, reason, fix, failures}: id is the
    // row's (microphoneAccess, desktopAccessibility, globalShortcuts,
    // virtualKeyboardHealth); state is ok, attention, off or working; reason
    // says which of the check's cases it is; fix is the action fix(id) runs,
    // or empty where there is nothing to run. failures are what went wrong
    // in the last thing tried on the check, [{step, error}]: step is enable
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
    // Whether openShortcutSettings knows this desktop's shortcut settings.
    bool canOpenShortcutSettings() const;
    // Whether uninstall can remove Speecher from this computer.
    bool canRemove() const;
    QString computerName() const;
    // Empty while no code is shown; otherwise waiting, sent, interrupted,
    // noNetwork or failed (PhoneTransferState).
    QString phoneTransfer() const;
    // What the code carries while it waits; empty otherwise.
    QString phoneLink() const;
    // What the last code shown sends: {terms, replacements, profiles} counts.
    QVariantMap phoneBundle() const;
    bool removing() const;
    // The last removal's report, or empty before one finishes:
    // {files: {removed, absent, failed}, the desktop's item names;
    // steps: [{part, outcome, error}], RemovalStep's, by name;
    // settingsDeleted; programFile}.
    QVariantMap removal() const;

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
    // The code for the phone exists from start to stop: Speecher listens for
    // the phone only while it shows. Starting again makes a new code.
    Q_INVOKABLE void startPhoneTransfer();
    Q_INVOKABLE void stopPhoneTransfer();
    // Removes Speecher from this computer, keeping the settings or not;
    // removal then holds the report. It starts once a virtual keyboard
    // operation under way ends, and none starts while removing. Once the
    // settings are deleted, nothing saves them again
    // (SettingsStore::deleteAll), and quitting is left.
    Q_INVOKABLE void uninstall(bool keepSettings);
    Q_INVOKABLE void quit();

signals:
    void changed();
    void phoneChanged();
    void removalChanged();
    // Something done out of sight that the window should mention. The app's
    // System says what went wrong in place (a check's failures, the removal
    // report), so only the preview's mock sends it.
    void announced(const QString &text, const QString &icon);

private:
    // The check with this id, as in checks, or an empty map.
    QVariantMap check(const QString &id) const;
    enum class RemovalAsked {
        KeepSettings,
        DeleteSettings,
    };

    void runFix(const QString &id, VirtualKeyboardSetup::AfterSetUp afterSetUp);
    // Forgets the check's failures as something new is tried on it.
    void clearFailures(const QString &id);

    ApplicationController *m_controller;
#ifdef Q_OS_LINUX
    // Starts the removal uninstall asked for, once the keyboard is idle.
    void startAskedRemoval();

    VirtualKeyboardSetup m_virtualKeyboard;
    SpeecherRemoval m_removal;
    // A removal uninstall asked for that waits for the keyboard.
    std::optional<RemovalAsked> m_removalAsked;
#endif
    QVariantMap m_removalReport;
    std::unique_ptr<PhoneTransfer> m_phone;
    QVariantMap m_phoneBundle;
    // Each check's failures, by check id, as checks shows them.
    QHash<QString, QVariantList> m_failures;
};

} // namespace speecher
