#pragma once

#include "app/SpeecherRemoval.h"
#include "app/VirtualKeyboardSetup.h"

#include <QObject>
#include <QQmlEngine>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

namespace speecher {

class ApplicationController;
class PhoneTransfer;

// The UI's System singleton: what Speecher knows and does about the computer
// it runs on. The permission and health checks with their fixes, the virtual
// keyboard's setup and typing test, the clipboard and opening links, sending
// settings to the phone, and removing Speecher. It carries states and core's
// messages; the UI words the states (health.js, the pages).
class QuickSystem final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(System)
    QML_SINGLETON
    Q_PROPERTY(bool accessibility READ accessibility NOTIFY changed)
    Q_PROPERTY(QVariantList checks READ checks NOTIFY changed)
    Q_PROPERTY(QStringList virtualKeyboardActions READ virtualKeyboardActions NOTIFY changed)
    Q_PROPERTY(QString virtualKeyboardTest READ virtualKeyboardTest NOTIFY changed)
    Q_PROPERTY(bool canOpenShortcutSettings READ canOpenShortcutSettings CONSTANT)
    Q_PROPERTY(QString computerName READ computerName CONSTANT)
    Q_PROPERTY(QString phoneTransfer READ phoneTransfer NOTIFY phoneChanged)
    Q_PROPERTY(QString phoneLink READ phoneLink NOTIFY phoneChanged)
    Q_PROPERTY(QVariantMap phoneBundle READ phoneBundle NOTIFY phoneChanged)
    Q_PROPERTY(bool removing READ removing NOTIFY removalChanged)
    Q_PROPERTY(QVariantMap removal READ removal NOTIFY removalChanged)

public:
    // The defaults touch the real computer; tests pass stand-ins.
    explicit QuickSystem(ApplicationController *controller, QObject *parent = nullptr);
    QuickSystem(ApplicationController *controller, VirtualKeyboardSetup::System virtualKeyboard,
                SpeecherRemoval::Steps removal, QObject *parent = nullptr);
    ~QuickSystem() override;

    // The engine's instance: the one the host made before loading any QML.
    static QuickSystem *create(QQmlEngine *, QJSEngine *);

    // Desktop accessibility is on, so Speecher can tell which app it pastes into.
    bool accessibility() const;
    // One entry per health row, {id, state, reason, fix}: id is the row's
    // (microphoneAccess, desktopAccessibility, globalShortcuts,
    // virtualKeyboardHealth); state is ok, attention, off or working; reason
    // says which of the check's cases it is; fix is the action fix(id) runs,
    // or empty where there is nothing to run.
    QVariantList checks() const;
    // What can be done to a virtual keyboard that is set up: disable, remove.
    QStringList virtualKeyboardActions() const;
    // The typing test that turns the keyboard on: off, ready (waiting for its
    // field), typing, or missed (the text never arrived).
    QString virtualKeyboardTest() const;
    // Whether openShortcutSettings knows this desktop's shortcut settings.
    bool canOpenShortcutSettings() const;
    QString computerName() const;
    // Empty while no code is shown; otherwise waiting, sent, interrupted,
    // noNetwork or failed (PhoneTransferState).
    QString phoneTransfer() const;
    // What the code carries while it waits; empty otherwise.
    QString phoneLink() const;
    // What the phone gets: {terms, replacements, profiles} counts.
    QVariantMap phoneBundle() const;
    bool removing() const;
    // The last removal's report, {removed, problems, settingsDeleted,
    // programFile}, or empty before one finishes.
    QVariantMap removal() const;

    // The check with this id, as in checks.
    Q_INVOKABLE QVariantMap check(const QString &id) const;
    // Runs the fix the check offers. A fix that fails says why in announced.
    Q_INVOKABLE void fix(const QString &id);
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
    // removal then holds the report.
    Q_INVOKABLE void uninstall(bool keepSettings);
    Q_INVOKABLE void quit();

signals:
    void changed();
    void phoneChanged();
    void removalChanged();
    // Something done out of sight that the window should mention, such as a
    // fix that failed, in core's words.
    void announced(const QString &text, const QString &icon);

private:
    ApplicationController *m_controller;
    VirtualKeyboardSetup m_virtualKeyboard;
    SpeecherRemoval m_removal;
    QVariantMap m_removalReport;
    std::unique_ptr<PhoneTransfer> m_phone;
};

} // namespace speecher
